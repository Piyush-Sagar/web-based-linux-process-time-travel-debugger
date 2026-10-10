#include "common.hpp"
extern "C" [[noreturn]] void _start() {
    watch_state[0] = 1;
    call(318, (long)&watch_state[1], 8, 0);
    call(228, 0, (long)&watch_state[2]);
    const char message[] = "recorded once\n";
    call(1, 1, (long)message, sizeof(message)-1);
    for (u64 i=0; i<5; ++i) { watch_state[0] = watch_state[0] + i + 3; watch_state[4] = watch_state[1] ^ watch_state[0]; }
    watch_state[5] = 0xfeed;
    *reinterpret_cast<volatile u64*>(0) = 7;
    finish(0);
}
