#include "common.hpp"
extern "C" [[noreturn]] void _start() { watch_state[0] = 9; call(39); finish(0); }
