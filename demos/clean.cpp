#include "common.hpp"
extern "C" [[noreturn]] void _start() {
    for (u64 i=0;i<4;++i) watch_state[i] = i*11+2;
    finish(0);
}
