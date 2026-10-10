#include "engine.hpp"
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
namespace ttd {
namespace {
void require(bool ok,const std::string& what) { if(!ok) throw std::runtime_error(what); }
long pt(enum __ptrace_request op,pid_t pid,void* address=nullptr,void* data=nullptr) {
    errno=0; long result=ptrace(op,pid,address,data);
    if(result==-1 && errno) throw std::runtime_error("ptrace: "+std::string(strerror(errno)));
    return result;
}
std::string hex(const void* data,size_t n) {
    auto p=static_cast<const unsigned char*>(data); std::string result; result.reserve(n*2);
    const char* digits="0123456789abcdef";
    for(size_t i=0;i<n;++i) { result+=digits[p[i]>>4]; result+=digits[p[i]&15]; } return result;
}
std::vector<unsigned char> unhex(const std::string& value) {
    require(value.size()%2==0,"invalid hex length"); std::vector<unsigned char> result;
    auto digit=[](char c)->unsigned { if(c>='0' && c<='9') return c-'0'; if(c>='a' && c<='f') return c-'a'+10; throw std::runtime_error("invalid hex"); };
    for(size_t i=0;i<value.size();i+=2) result.push_back((digit(value[i])<<4)|digit(value[i+1]));
    return result;
}
std::string h64(unsigned long long x) { std::ostringstream s; s<<"0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<x; return s.str(); }
std::string digest(const std::string& s) { unsigned long long h=14695981039346656037ULL; for(unsigned char c:s) {h^=c;h*=1099511628211ULL;} return h64(h); }
std::string filebytes(const std::string& path) { std::ifstream f(path,std::ios::binary); require(bool(f),"cannot read "+path); return {std::istreambuf_iterator<char>(f),{}}; }
unsigned long symbol(const std::string& path) {
    auto bytes=filebytes(path); require(bytes.size()>=sizeof(Elf64_Ehdr),"invalid ELF"); Elf64_Ehdr e; memcpy(&e,bytes.data(),sizeof(e));
    require(!memcmp(e.e_ident,ELFMAG,SELFMAG) && e.e_type==ET_EXEC && e.e_machine==EM_X86_64,"requires static x86-64 ET_EXEC demo");
    require(e.e_shentsize==sizeof(Elf64_Shdr),"invalid section table");
    auto section=[&](size_t i) { require(i<e.e_shnum && e.e_shoff+(i+1)*sizeof(Elf64_Shdr)<=bytes.size(),"invalid ELF sections"); Elf64_Shdr s; memcpy(&s,bytes.data()+e.e_shoff+i*sizeof(s),sizeof(s)); require(s.sh_type==SHT_NOBITS || s.sh_offset+s.sh_size<=bytes.size(),"invalid ELF section bounds"); return s; };
    for(size_t i=0;i<e.e_shnum;++i) { auto s=section(i); if(s.sh_type!=SHT_SYMTAB) continue; auto strings=section(s.sh_link);
        for(size_t o=0;o+sizeof(Elf64_Sym)<=s.sh_size;o+=sizeof(Elf64_Sym)) { Elf64_Sym sym; memcpy(&sym,bytes.data()+s.sh_offset+o,sizeof(sym));
            if(sym.st_name+12<=strings.sh_size && !memcmp(bytes.data()+strings.sh_offset+sym.st_name,"watch_state",12)) return sym.st_value;
        }
    } throw std::runtime_error("watch_state symbol missing");
}
json register_json(const user_regs_struct& r) {
    json j;
#define REG(name) j[#name]=h64(r.name)
    REG(rax);REG(rbx);REG(rcx);REG(rdx);REG(rsi);REG(rdi);REG(rbp);REG(rsp);REG(r8);REG(r9);REG(r10);REG(r11);REG(r12);REG(r13);REG(r14);REG(r15);REG(rip);REG(eflags);REG(orig_rax);REG(cs);REG(ss);REG(ds);REG(es);REG(fs);REG(gs);REG(fs_base);REG(gs_base);
#undef REG
    return j;
}
json arguments(const user_regs_struct& r) { return json::array({h64(r.rdi),h64(r.rsi),h64(r.rdx),h64(r.r10),h64(r.r8),h64(r.r9)}); }
int wait_child(pid_t pid) { int status; pid_t result; do {result=waitpid(pid,&status,0);} while(result<0 && errno==EINTR); require(result==pid,"waitpid failed"); return status; }
}
Engine::~Engine() { kill_child(); }
json Engine::demos() { return json::array({{{"id","crash"},{"description","Random/time inputs, output, loop, SIGSEGV"}},{{"id","clean"},{"description","Deterministic computation and exit(0)"}},{{"id","forbidden"},{"description","Explicit rejection of getpid syscall"}},{{"id","loop"},{"description","Pause and recording limit demonstration"}}}); }
void Engine::kill_child() {
    if(pid_>0) { ::kill(pid_,SIGKILL); int status; while(waitpid(pid_,&status,0)<0 && errno==EINTR) {} pid_=-1; }
    if(output_fd_>=0){close(output_fd_);output_fd_=-1;}
}
void Engine::spawn() {
    int pipes[2];require(pipe2(pipes,O_CLOEXEC)==0,"output pipe failed");observed_output_.clear();
    pid_=fork();if(pid_<0){close(pipes[0]);close(pipes[1]);throw std::runtime_error("fork failed");}
    if(pid_==0) {
        close(pipes[0]);
        int nullfd=open("/dev/null",O_RDWR); if(nullfd<0) _exit(125);
        dup2(nullfd,0);dup2(pipes[1],1);dup2(pipes[1],2);if(nullfd>2) close(nullfd);if(pipes[1]>2)close(pipes[1]);
        if(personality(ADDR_NO_RANDOMIZE)==-1) _exit(125);
        if(ptrace(PTRACE_TRACEME,0,nullptr,nullptr)==-1) _exit(125);
        char* argv[]={const_cast<char*>(executable_.c_str()),nullptr}; char* env[]={nullptr};
        execve(executable_.c_str(),argv,env); _exit(126);
    }
    close(pipes[1]);output_fd_=pipes[0];require(fcntl(output_fd_,F_SETFL,O_NONBLOCK)!=-1,"output pipe setup failed");
    int status=wait_child(pid_);
    if(!WIFSTOPPED(status) || WSTOPSIG(status)!=SIGTRAP) { pid_=-1; throw std::runtime_error("launch failed: exec/ptrace/ASLR personality unavailable"); }
    pt(PTRACE_SETOPTIONS,pid_,nullptr,reinterpret_cast<void*>(PTRACE_O_EXITKILL));
    // The synthetic exec-stop on this kernel reports the reserved EFLAGS bit 1
    // clear, and SETREGS cannot change it there. Enter a genuine instruction
    // stop before taking or restoring architectural checkpoints.
    pt(PTRACE_SINGLESTEP,pid_);
    int ready=wait_child(pid_);
    require(WIFSTOPPED(ready) && WSTOPSIG(ready)==SIGTRAP,"demo bootstrap must stop after one instruction");
    pending_signal_=0;status_="paused";
}
user_regs_struct Engine::regs() const { user_regs_struct r{};pt(PTRACE_GETREGS,pid_,nullptr,&r);return r; }
void Engine::setregs(const user_regs_struct& r) {pt(PTRACE_SETREGS,pid_,nullptr,const_cast<user_regs_struct*>(&r));}
std::vector<unsigned char> Engine::read(unsigned long address,size_t count) const {
    require(count<=256*1024,"memory read bound exceeded"); std::vector<unsigned char> data(count);
    for(size_t i=0;i<count;i+=sizeof(long)) {long word=pt(PTRACE_PEEKDATA,pid_,reinterpret_cast<void*>(address+i));memcpy(data.data()+i,&word,std::min(sizeof(long),count-i));} return data;
}
void Engine::write(unsigned long address,const std::vector<unsigned char>& data) {
    for(size_t i=0;i<data.size();i+=sizeof(long)) {long word=0;size_t n=std::min(sizeof(long),data.size()-i);if(n<sizeof(long)) word=pt(PTRACE_PEEKDATA,pid_,reinterpret_cast<void*>(address+i));memcpy(&word,data.data()+i,n);pt(PTRACE_POKEDATA,pid_,reinterpret_cast<void*>(address+i),reinterpret_cast<void*>(word));}
}
std::vector<Region> Engine::regions() const {
    std::ifstream f("/proc/"+std::to_string(pid_)+"/maps"); require(bool(f),"cannot read mappings"); std::string line; std::vector<Region> result;size_t total=0;
    while(std::getline(f,line)) {std::istringstream s(line);std::string range,perm,offset,dev,name;unsigned long inode;s>>range>>perm>>offset>>dev>>inode;std::getline(s,name);
        if(perm.size()<4 || perm[1]!='w') continue;
        require(perm=="rw-p","unsupported writable mapping permissions");size_t sep=range.find('-');unsigned long start=std::stoul(range.substr(0,sep),nullptr,16),end=std::stoul(range.substr(sep+1),nullptr,16);
        bool demo_bss=(name.find_first_not_of(' ')==std::string::npos || name.find("[heap]")!=std::string::npos) && start<=watch_ && watch_+64<=end && end-start<=4096;
        std::string decoded=name;
        for(size_t at=0;(at=decoded.find("\\040",at))!=std::string::npos;) decoded.replace(at,4," ");
        require(demo_bss || decoded.find(executable_)!=std::string::npos || name.find("[stack]")!=std::string::npos,"unsupported writable mapping: "+line);total+=end-start;require(total<=256*1024,"checkpoint memory bound exceeded");result.push_back({start,end,name});
    } require(!result.empty(),"no writable mappings"); return result;
}
json Engine::capture(const std::string& kind,int signal,const json& syscall) {
    json event={{"step",cursor_},{"kind",kind},{"signal",signal},{"status",status_},{"syscall",syscall}};
    if(pid_>0) { auto r=regs();auto memory=read(watch_,64);event["registers"]=register_json(r);event["raw_regs"]=hex(&r,sizeof(r));event["memory"]={{"address",h64(watch_)},{"hex",hex(memory.data(),memory.size())},{"label","watch_state[8]"}}; }
    return event;
}
void Engine::checkpoint() {
    json cp={{"step",cursor_},{"raw_regs",live_.at("raw_regs")},{"regions",json::array()}};
    std::vector<unsigned char> extended(16384);iovec io{extended.data(),extended.size()};pt(PTRACE_GETREGSET,pid_,reinterpret_cast<void*>(NT_X86_XSTATE),&io);extended.resize(io.iov_len);cp["xstate"]=hex(extended.data(),extended.size());
    for(auto& region:regions()) {auto data=read(region.start,region.end-region.start);cp["regions"].push_back({{"start",region.start},{"end",region.end},{"name",region.name},{"hex",hex(data.data(),data.size())}});}
    trace_["checkpoints"].push_back(cp);
}
json Engine::launch(const std::string& demo) {
    bool known=false;for(auto& d:demos()) if(d["id"]==demo) known=true;require(known,"unsupported demo (use bundled executable ID)");kill_child();
    demo_=demo;executable_=std::string(DEMO_DIR)+"/demo_"+demo;watch_=symbol(executable_);cursor_=0;mode_="recording";
    auto manifest=json::parse(filebytes(std::string(DEMO_DIR)+"/demos.json"));
    require(manifest.at(demo).get<std::string>()==digest(filebytes(executable_)),"demo differs from build-time identity allowlist; rebuild controlled demos");
    trace_={{"version",1},{"demo",demo},{"executable_hash",digest(filebytes(executable_))},{"events",json::array()},{"checkpoints",json::array()},{"max_steps",limit}};
    spawn();live_=capture("launch");trace_["events"].push_back(live_);checkpoint();return state();
}
void Engine::validate_syscall(const user_regs_struct& r) const {
    switch(r.rax) {
    case 318: require(r.rsi<=4096 && r.rdx==0 && r.rdi>=watch_ && r.rdi<=watch_+64 && r.rsi<=watch_+64-r.rdi,"getrandom limited to watch_state, flags=0");break;
    case 228: require(r.rdi<=1 && r.rsi>=watch_ && r.rsi<=watch_+48,"clock_gettime limited to REALTIME/MONOTONIC and watch_state");break;
    case 1: require((r.rdi==1 || r.rdi==2) && r.rdx<=4096,"write limited to stdout/stderr and 4 KiB");break;
    case 60:case 231:break;
    default:throw std::runtime_error("unsupported syscall "+std::to_string(r.rax)+" rejected before execution");
    }
}
json Engine::execute(const json* expected) {
    require(pid_>0 && (status_=="paused" || status_=="crashed"),"execution is not resumable");
    require(cursor_<limit,"recording instruction bound reached");
    auto before=regs();auto instruction=read(before.rip,2);bool is_syscall=instruction[0]==0x0f && instruction[1]==0x05;
    json call=nullptr;
    if(is_syscall && !pending_signal_) {
        try {validate_syscall(before);} catch(...) {status_="rejected";live_=capture("rejected");live_["error"]="unsupported syscall or arguments";throw;}
        call={{"number",before.rax},{"arguments",arguments(before)},{"output",""},{"output_address",0},{"write_hex",""}};
        if(before.rax==1) {auto bytes=read(before.rsi,before.rdx);call["write_hex"]=hex(bytes.data(),bytes.size());}
        if(expected) {
            require(expected->at("syscall").at("number")==call["number"] && expected->at("syscall").at("arguments")==call["arguments"] && expected->at("syscall").at("write_hex")==call["write_hex"],"replay syscall input divergence");
            if(before.rax!=60 && before.rax!=231) {auto suppressed=before;suppressed.rax=static_cast<unsigned long long>(-1);setregs(suppressed);}
        }
    }
    pt(PTRACE_SINGLESTEP,pid_,nullptr,reinterpret_cast<void*>(static_cast<long>(pending_signal_)));pending_signal_=0;
    int result=wait_child(pid_);++cursor_;
    if(WIFEXITED(result) || WIFSIGNALED(result)) {status_=WIFEXITED(result)?"exited":"terminated";pid_=-1;live_=capture("exit",WIFSIGNALED(result)?WTERMSIG(result):0,call);live_["exit_code"]=WIFEXITED(result)?WEXITSTATUS(result):-1;return live_;}
    require(WIFSTOPPED(result),"unexpected wait status");int signal=WSTOPSIG(result);status_="paused";
    if(signal!=SIGTRAP) {
        require(signal==SIGSEGV || signal==SIGILL || signal==SIGFPE || signal==SIGBUS,"unsupported asynchronous signal");pending_signal_=signal;status_="crashed";
    }
    if(is_syscall && !call.is_null()) {
        auto after=regs();
        if(expected) {
            auto recorded=expected->at("syscall");after.rax=std::stoull(recorded.at("result").get<std::string>(),nullptr,16);after.orig_rax=before.rax;setregs(after);
            auto output=unhex(recorded.at("output"));require(output.size()<=4096,"replay output bound");if(!output.empty()) write(recorded.at("output_address").get<unsigned long>(),output);
            call=recorded;
        } else {
            call["result"]=h64(after.rax);long returned=static_cast<long>(after.rax);unsigned long address=0;size_t count=0;
            if(before.rax==318 && returned>0) {address=before.rdi;count=static_cast<size_t>(returned);require(count<=before.rsi,"invalid getrandom result");}
            if(before.rax==228 && returned==0) {address=before.rsi;count=16;}
            if(count) {auto bytes=read(address,count);call["output"]=hex(bytes.data(),bytes.size());call["output_address"]=address;}
        }
        if(before.rax==1) {
            std::vector<unsigned char> emitted;unsigned char chunk[4096];ssize_t n;
            while((n=::read(output_fd_,chunk,sizeof(chunk)))>0)emitted.insert(emitted.end(),chunk,chunk+n);
            require(n==0 || errno==EAGAIN || errno==EWOULDBLOCK,"cannot capture child output");
            if(expected)require(emitted.empty(),"replay repeated an external output side effect");
            else call["actual_output_hex"]=hex(emitted.data(),emitted.size());
            require(observed_output_.size()+emitted.size()<=65536,"captured output bound exceeded");
            observed_output_.insert(observed_output_.end(),emitted.begin(),emitted.end());
        }
    }
    live_=capture(signal==SIGTRAP?(is_syscall?"syscall":"instruction"):"signal",signal==SIGTRAP?0:signal,call);return live_;
}
void Engine::verify(const json& actual,const json& expected) const {
    require(actual.at("kind")==expected.at("kind") && actual.at("signal")==expected.at("signal"),"replay event divergence at "+std::to_string(cursor_));
    if(actual.contains("exit_code"))require(actual.at("exit_code")==expected.at("exit_code"),"replay exit code divergence");
    if(actual.contains("registers")) {
        for(auto& [name,value]:expected.at("registers").items()) require(actual.at("registers").at(name)==value,"replay register "+name+" divergence at "+std::to_string(cursor_)+": actual "+actual.at("registers").at(name).get<std::string>()+", expected "+value.get<std::string>());
        require(actual.at("memory")==expected.at("memory"),"replay memory divergence at "+std::to_string(cursor_));
    }
}
json Engine::step() {
    if(mode_=="replay") {
        require(cursor_+1<size(),"end of recorded replay; branching unsupported");auto expected=trace_["events"][cursor_+1];try {auto actual=execute(&expected);verify(actual,expected);}catch(...) {status_="diverged";kill_child();throw;}return state();
    }
    auto actual=execute(nullptr);trace_["events"].push_back(actual);
    if(status_=="paused" && cursor_%checkpoint_interval==0) checkpoint();
    if(status_=="paused" && cursor_==limit) status_="bounded";
    return state();
}
json Engine::run(size_t count) {require(count>0 && count<=limit,"invalid step count");for(size_t i=0;i<count && runnable();++i) step();return state();}
json Engine::restore(size_t target) {
    require(target<size(),"target out of range");require(trace_["events"][target].contains("registers"),"cannot restore exited state");
    require(digest(filebytes(executable_))==trace_.at("executable_hash").get<std::string>(),"executable changed; restoration refused");
    json cp=trace_["checkpoints"][0];for(auto& c:trace_["checkpoints"]) if(c["step"].get<size_t>()<=target) cp=c;
    kill_child();live_=nullptr;
    try {
        spawn();auto current=regions();require(current.size()==cp["regions"].size(),"mapping count mismatch");
        // Restore every writable page, not only the 64 inspector bytes.
        for(size_t i=0;i<current.size();++i) {
            auto& saved=cp["regions"][i];
            require(saved["start"]==current[i].start && saved["end"]==current[i].end && saved["name"]==current[i].name,"mapping layout mismatch; restoration refused");
            auto bytes=unhex(saved["hex"]);require(bytes.size()==current[i].end-current[i].start,"invalid checkpoint region");write(current[i].start,bytes);
        }
        auto xstate=unhex(cp.at("xstate"));require(xstate.size()<=16384,"extended state too large");iovec io{xstate.data(),xstate.size()};
        pt(PTRACE_SETREGSET,pid_,reinterpret_cast<void*>(NT_X86_XSTATE),&io);
        auto raw=unhex(cp.at("raw_regs"));require(raw.size()==sizeof(user_regs_struct),"invalid checkpoint registers");
        user_regs_struct r{};memcpy(&r,raw.data(),raw.size());setregs(r);cursor_=cp["step"];mode_="replay";pending_signal_=0;
        live_=capture(trace_["events"][cursor_]["kind"],0,trace_["events"][cursor_]["syscall"]);verify(live_,trace_["events"][cursor_]);
        // Ordinary instructions execute; only syscall outputs come from the log.
        while(cursor_<target) {auto expected=trace_["events"][cursor_+1];auto actual=execute(&expected);verify(actual,expected);}
    } catch(...) {status_="diverged";mode_="history";kill_child();live_=nullptr;throw;}
    auto result=state();result["restoration"]={{"checkpoint_step",cp["step"]},{"target_step",target},{"executed_instructions",target-cp["step"].get<size_t>()},{"verified",true},{"syscalls","recorded results injected; external writes suppressed"}};return result;
}
json Engine::state() const {return {{"demo",demo_},{"pid",pid_},{"status",status_},{"mode",mode_},{"cursor",cursor_},{"recorded_steps",trace_["events"].size()},{"observed_output_hex",hex(observed_output_.data(),observed_output_.size())},{"checkpoint_steps",[&]{json a=json::array();for(auto& cp:trace_["checkpoints"])a.push_back(cp["step"]);return a;}()},{"live",live_}};}
json Engine::history(size_t step) const {require(step<size(),"historical step out of range");return {{"mode","history"},{"execution_restored",false},{"event",trace_["events"][step]}};}
json Engine::events() const {return trace_["events"];}
json Engine::save(const std::string& path) const {
    require(size()>0,"no trace");json envelope={{"checksum",digest(trace_.dump())},{"trace",trace_}};auto bytes=envelope.dump();require(bytes.size()<=32*1024*1024,"persistent trace bound exceeded");
    auto parent=std::filesystem::path(path).parent_path();if(!parent.empty()) std::filesystem::create_directories(parent);std::ofstream f(path+".tmp",std::ios::binary);f<<bytes;require(bool(f),"cannot save trace");f.close();std::filesystem::rename(path+".tmp",path);return {{"path",path},{"bytes",bytes.size()}};
}
json Engine::load(const std::string& path) {
    require(std::filesystem::file_size(path)<=32*1024*1024,"trace file too large");auto envelope=json::parse(filebytes(path));auto trace=envelope.at("trace");require(envelope.at("checksum")==digest(trace.dump()),"trace checksum mismatch");require(trace.at("version")==1,"unsupported trace version");
    std::string demo=trace.at("demo");bool known=false;for(auto& d:demos())if(d["id"]==demo)known=true;require(known,"unsupported trace demo");
    auto path_exe=std::string(DEMO_DIR)+"/demo_"+demo;require(trace.at("executable_hash")==digest(filebytes(path_exe)),"executable identity mismatch");require(trace["events"].size()>0 && trace["events"].size()<=limit+1 && trace["checkpoints"].size()>0,"invalid trace bounds");
    auto manifest=json::parse(filebytes(std::string(DEMO_DIR)+"/demos.json"));require(manifest.at(demo)==trace.at("executable_hash"),"demo not in build-time identity allowlist");
    kill_child();observed_output_.clear();trace_=trace;demo_=demo;executable_=path_exe;watch_=symbol(executable_);status_="loaded";mode_="history";cursor_=0;live_=nullptr;return state();
}
json Engine::terminate() {
    bool was_live=pid_>0;kill_child();status_="terminated";
    if(was_live && mode_=="recording" && cursor_<limit) {++cursor_;live_=capture("exit",SIGKILL);live_["exit_code"]=-1;trace_["events"].push_back(live_);}
    else live_=nullptr;
    return state();
}
}
