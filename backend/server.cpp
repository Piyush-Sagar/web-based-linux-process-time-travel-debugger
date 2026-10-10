#include "engine.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <filesystem>
namespace net=boost::asio;
namespace beast=boost::beast;
namespace http=beast::http;
namespace websocket=beast::websocket;
using tcp=net::ip::tcp;
using ttd::json;
class WebSocket;
struct Session { ttd::Engine engine; bool running=false; std::string error; };
struct Service {
    net::io_context& io;
    net::steady_timer timer;
    std::map<std::string,std::unique_ptr<Session>> sessions;
    std::vector<std::weak_ptr<WebSocket>> subscribers;
    unsigned next_id=1;
    unsigned short port;
    std::string traces,assets;
    Service(net::io_context& context,unsigned short p,std::string directory,std::string root):io(context),timer(io),port(p),traces(std::move(directory)),assets(std::move(root)) {
        std::filesystem::create_directories(traces);
        for(auto& file:std::filesystem::directory_iterator(traces)) {
            std::string name=file.path().stem().string();
            if(file.path().extension()!=".json" || !std::regex_match(name,std::regex("s[0-9]{1,9}")))continue;
            next_id=std::max(next_id,static_cast<unsigned>(std::stoul(name.substr(1)))+1);
            if(sessions.size()>=8)continue;
            try {auto s=std::make_unique<Session>();s->engine.load(file.path());sessions[name]=std::move(s);}catch(const std::exception& e){std::cerr<<"Skipped trace "<<name<<": "<<e.what()<<"\n";}
        }
        tick();
    }
    json state(const std::string& id) {auto& s=*sessions.at(id);auto j=s.engine.state();j["id"]=id;j["running"]=s.running;j["error"]=s.error;return j;}
    void save(const std::string& id) {sessions.at(id)->engine.save(traces+"/"+id+".json");}
    void broadcast(const std::string& id);
    void tick() {
        timer.expires_after(std::chrono::milliseconds(25));timer.async_wait([this](beast::error_code ec) {
            if(ec)return;
            for(auto& [id,s]:sessions) if(s->running) {
                try {s->engine.run(8);if(!s->engine.runnable())s->running=false;}
                catch(const std::exception& e){s->running=false;s->error=e.what();}
                if(!s->running) {try {save(id);}catch(const std::exception& e){s->error=e.what();}}
                broadcast(id);
            } tick();
        });
    }
    bool allowed(const http::request<http::string_body>& req) const {
        auto host=std::string(req[http::field::host]);
        if(host!="localhost:"+std::to_string(port) && host!="127.0.0.1:"+std::to_string(port))return false;
        auto origin=std::string(req[http::field::origin]);
        return origin.empty() || origin=="http://localhost:"+std::to_string(port) || origin=="http://127.0.0.1:"+std::to_string(port);
    }
    http::response<http::string_body> handle(const http::request<http::string_body>& req) {
        http::response<http::string_body> response{http::status::ok,11};response.set(http::field::content_type,"application/json");response.set(http::field::cache_control,"no-store");response.keep_alive(false);
        auto reply=[&](json j,http::status status=http::status::ok){response.result(status);response.body()=j.dump();response.prepare_payload();return response;};
        if(!allowed(req))return reply({{"error","only same-origin loopback requests are allowed"}},http::status::forbidden);
        try {
            const std::string target(req.target());auto q=target.find('?');std::string path=target.substr(0,q);
            const auto method=req.method();
            if(path=="/api/health" && method==http::verb::get) return reply({{"ok",true},{"engine","custom-ptrace"},{"platform","linux-x86_64"},{"max_steps",ttd::Engine::limit}});
            if(path=="/api/demos" && method==http::verb::get)return reply(ttd::Engine::demos());
            if(path=="/api/sessions") {
                if(method==http::verb::get){json list=json::array();for(auto& [id,s]:sessions)list.push_back(state(id));return reply(list);}
                if(method==http::verb::post){if(sessions.size()>=8)throw std::runtime_error("8 session limit; remove a session first");auto body=json::parse(req.body());auto s=std::make_unique<Session>();s->engine.launch(body.at("demo"));auto id="s"+std::to_string(next_id++);sessions[id]=std::move(s);save(id);broadcast(id);return reply(state(id),http::status::created);}
            }
            std::smatch match;
            if(std::regex_match(path,match,std::regex("/api/sessions/(s[0-9]{1,9})(/events|/state|/control)?"))) {
                std::string id=match[1],suffix=match[2];
                if(!sessions.contains(id))return reply({{"error","session not found"}},http::status::not_found);
                auto& s=*sessions.at(id);
                if(suffix.empty() && method==http::verb::get)return reply(state(id));
                if(suffix.empty() && method==http::verb::delete_) {sessions.erase(id);std::filesystem::remove(traces+"/"+id+".json");return reply({{"deleted",id}});}
                if(suffix=="/events" && method==http::verb::get)return reply(s.engine.events());
                if(suffix=="/state" && method==http::verb::get) {
                    if(q==std::string::npos)return reply(state(id));
                    std::string query=target.substr(q+1);if(!std::regex_match(query,std::regex("step=[0-9]{1,5}")))throw std::runtime_error("expected step=N");return reply(s.engine.history(std::stoul(query.substr(5))));
                }
                if(suffix=="/control" && method==http::verb::post) {
                    auto body=json::parse(req.body());std::string action=body.at("action");json result;
                    if(s.running && action!="pause" && action!="terminate")throw std::runtime_error("pause before step or restore");
                    s.error.clear();
                    try {
                        if(action=="step")result=s.engine.step();
                        else if(action=="continue") {if(!s.engine.runnable())throw std::runtime_error("execution not runnable; restore an earlier step");s.running=true;}
                        else if(action=="pause")s.running=false;
                        else if(action=="terminate"){s.running=false;result=s.engine.terminate();}
                        else if(action=="restore")result=s.engine.restore(body.at("step"));
                        else throw std::runtime_error("unsupported control action");
                    }catch(const std::exception& e){s.error=e.what();broadcast(id);return reply({{"error",e.what()},{"state",state(id)}},http::status::conflict);}
                    if(!s.running)save(id);
                    broadcast(id);auto updated=state(id);if(result.contains("restoration"))updated["restoration"]=result["restoration"];return reply(updated);
                }
            }
            if(path.starts_with("/api/"))return reply({{"error","unsupported API operation"}},http::status::not_found);
            if(method==http::verb::get && (path=="/" || std::regex_match(path,std::regex("/assets/[A-Za-z0-9_.-]+")))) {
                std::string file=assets+(path=="/"?"/index.html":path);std::ifstream input(file,std::ios::binary);if(!input)return reply({{"error","frontend unavailable; run npm ci && npm run build in frontend"}},http::status::not_found);
                response.body()={std::istreambuf_iterator<char>(input),{}};
                response.set(http::field::content_type,path.ends_with(".js")?"text/javascript":path.ends_with(".css")?"text/css":"text/html");
                response.set("Content-Security-Policy","default-src 'self'; connect-src 'self' ws://localhost:* ws://127.0.0.1:*; style-src 'self' 'unsafe-inline'; script-src 'self'; frame-ancestors 'none'");
                response.set("X-Content-Type-Options","nosniff");response.prepare_payload();return response;
            }
            return reply({{"error","not found"}},http::status::not_found);
        }catch(const std::exception& e){return reply({{"error",e.what()}},http::status::bad_request);}
    }
};
class WebSocket:public std::enable_shared_from_this<WebSocket> {
    websocket::stream<beast::tcp_stream> stream;
    beast::flat_buffer buffer;
    std::deque<std::string> messages;
public:
    WebSocket(tcp::socket&& socket):stream(std::move(socket)){}
    void start(http::request<http::string_body> req) {
        stream.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));stream.read_message_max(4096);
        stream.async_accept(req,[self=shared_from_this()](beast::error_code ec){if(!ec){self->send({{"type","connected"},{"data_source","live ptrace backend"}});self->read();}});
    }
    void send(const json& value) {
        if(!stream.is_open())return;
        if(messages.size()>=16){beast::error_code ec;beast::get_lowest_layer(stream).socket().close(ec);return;}
        messages.push_back(value.dump());if(messages.size()==1)write();
    }
private:
    void write() {stream.text(true);stream.async_write(net::buffer(messages.front()),[self=shared_from_this()](beast::error_code ec,size_t){if(ec)return;self->messages.pop_front();if(!self->messages.empty())self->write();});}
    void read() {stream.async_read(buffer,[self=shared_from_this()](beast::error_code ec,size_t){if(ec)return;self->buffer.consume(self->buffer.size());self->read();});}
};
void Service::broadcast(const std::string& id) {
    json update={{"type","session"},{"session",state(id)}};
    std::erase_if(subscribers,[](auto& p){return p.expired();});for(auto& client:subscribers)if(auto socket=client.lock())socket->send(update);
}
class HttpConnection:public std::enable_shared_from_this<HttpConnection> {
    beast::tcp_stream stream;
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    std::shared_ptr<http::response<http::string_body>> response;
    Service& service;
public:
    HttpConnection(tcp::socket&& socket,Service& s):stream(std::move(socket)),service(s){}
    void start() {
        parser.body_limit(8192);stream.expires_after(std::chrono::seconds(10));
        http::async_read(stream,buffer,parser,[self=shared_from_this()](beast::error_code ec,size_t){
            if(ec)return;
            auto req=self->parser.release();
            if(websocket::is_upgrade(req) && req.target()=="/ws" && self->service.allowed(req)) {
                std::erase_if(self->service.subscribers,[](auto& p){return p.expired();});
                if(self->service.subscribers.size()<16){auto ws=std::make_shared<WebSocket>(self->stream.release_socket());self->service.subscribers.push_back(ws);ws->start(std::move(req));return;}
            }
            self->response=std::make_shared<http::response<http::string_body>>(self->service.handle(req));
            http::async_write(self->stream,*self->response,[self](beast::error_code,size_t){beast::error_code ignored;self->stream.socket().shutdown(tcp::socket::shutdown_send,ignored);});
        });
    }
};
int main(int argc,char** argv) {
    try {
        unsigned long requested=argc>1?std::stoul(argv[1]):8080;if(requested<1024 || requested>65535)throw std::runtime_error("port must be 1024..65535");auto port=static_cast<unsigned short>(requested);
        net::io_context io(1);Service service(io,port,argc>2?argv[2]:"traces",argc>3?argv[3]:FRONTEND_DIR);
        tcp::acceptor acceptor(io,{net::ip::make_address("127.0.0.1"),port});
        std::function<void()> accept=[&]{acceptor.async_accept([&](beast::error_code ec,tcp::socket socket){if(!ec)std::make_shared<HttpConnection>(std::move(socket),service)->start();if(acceptor.is_open())accept();});};accept();
        net::signal_set signals(io,SIGINT,SIGTERM);signals.async_wait([&](beast::error_code,int){
            for(auto& [id,s]:service.sessions)try{s->running=false;service.save(id);}catch(const std::exception& e){std::cerr<<"Save on shutdown: "<<e.what()<<"\n";}
            io.stop();
        });
        std::cout<<"Debugger running at http://localhost:"<<port<<" (Linux ptrace worker, loopback only)"<<std::endl;io.run();
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
