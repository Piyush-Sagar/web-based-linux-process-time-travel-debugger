#include "common.hpp"
extern "C" [[noreturn]] void _start() { for (;;) watch_state[0] = watch_state[0] + 1; }
