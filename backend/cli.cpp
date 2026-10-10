#include "engine.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

struct Request { std::string line; std::promise<ttd::json> result; };

int main(int argc,char**) {
    if(argc!=1) {std::cerr<<"ttd: JSON-line CLI on stdin. See README.md.\n";return 2;}
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::shared_ptr<Request>> requests;
    bool stopping=false;
    // Launch, waitpid and ptrace always run on this same owning thread.
    std::thread worker([&] {
        ttd::Engine engine;
        bool running=false;
        std::string background_error;
        auto state=[&] {auto s=engine.state();s["running"]=running;s["background_error"]=background_error;return s;};
        for(;;) {
            std::shared_ptr<Request> request;
            {
                std::unique_lock lock(mutex);
                if(running)ready.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopping||!requests.empty();});
                else ready.wait(lock,[&]{return stopping||!requests.empty();});
                if(stopping&&requests.empty())break;
                if(!requests.empty()){request=requests.front();requests.pop_front();}
            }
            if(!request) {
                try {engine.run(8);if(!engine.runnable())running=false;}
                catch(const std::exception& e){background_error=e.what();running=false;}
                continue;
            }
            try {
                auto command=ttd::json::parse(request->line);
                std::string action=command.at("action");ttd::json result;
                if(running&&(action=="launch"||action=="step"||action=="restore"||action=="load"||action=="continue"))throw std::runtime_error("pause before changing live execution");
                if(action=="launch"){background_error.clear();result=engine.launch(command.at("demo"));}
                else if(action=="step")result=engine.step();
                else if(action=="continue") {
                    if(command.value("async",false)) {
                        if(!engine.runnable())throw std::runtime_error("execution is not runnable");
                        running=true;background_error.clear();result=state();
                    } else result=engine.run(command.value("count",ttd::Engine::limit));
                }
                else if(action=="pause"){running=false;result=state();}
                else if(action=="state")result=state();
                else if(action=="restore")result=engine.restore(command.at("step"));
                else if(action=="history")result=engine.history(command.at("step"));
                else if(action=="events")result=engine.events();
                else if(action=="save")result=engine.save(command.at("path"));
                else if(action=="load")result=engine.load(command.at("path"));
                else if(action=="terminate"){running=false;result=engine.terminate();}
                else throw std::runtime_error("unsupported action");
                request->result.set_value({{"ok",true},{"result",result}});
            } catch(const std::exception& e) {
                request->result.set_value({{"ok",false},{"error",e.what()},{"state",state()}});
            }
        }
    });
    std::string line;
    while(std::getline(std::cin,line)) {
        auto request=std::make_shared<Request>();request->line=line;auto future=request->result.get_future();
        {std::lock_guard lock(mutex);requests.push_back(request);}ready.notify_one();
        std::cout<<future.get().dump()<<std::endl;
    }
    {std::lock_guard lock(mutex);stopping=true;}ready.notify_one();worker.join();
}
