#pragma once
#include <nlohmann/json.hpp>
#include <sys/user.h>
#include <string>
#include <vector>
namespace ttd {
using json=nlohmann::json;
struct Region { unsigned long start,end; std::string name; };
class Engine {
public:
    static constexpr size_t limit=2000, checkpoint_interval=20;
    Engine()=default;
    ~Engine();
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    json launch(const std::string& demo);
    json step();
    json run(size_t count=limit);
    json restore(size_t target);
    json state() const;
    json history(size_t step) const;
    json events() const;
    json save(const std::string& path) const;
    json load(const std::string& path);
    json terminate();
    size_t size() const { return trace_["events"].size(); }
    bool runnable() const { return pid_>0 && status_=="paused"; }
    static json demos();
private:
    pid_t pid_=-1;
    int output_fd_=-1;
    std::vector<unsigned char> observed_output_;
    std::string demo_, executable_, status_="empty", mode_="recording";
    unsigned long watch_=0;
    size_t cursor_=0;
    int pending_signal_=0;
    json trace_={{"version",1},{"events",json::array()},{"checkpoints",json::array()}};
    json live_;
    void spawn();
    void kill_child();
    std::vector<Region> regions() const;
    std::vector<unsigned char> read(unsigned long address,size_t count) const;
    void write(unsigned long address,const std::vector<unsigned char>& data);
    user_regs_struct regs() const;
    void setregs(const user_regs_struct& r);
    json capture(const std::string& kind,int signal=0,const json& syscall=nullptr);
    void checkpoint();
    json execute(const json* expected);
    void validate_syscall(const user_regs_struct& r) const;
    void verify(const json& actual,const json& expected) const;
};
}
