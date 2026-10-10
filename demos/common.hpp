#pragma once
using u64 = unsigned long long;
extern "C" { volatile u64 watch_state[8] = {}; }
static long call(long n, long a=0, long b=0, long c=0) {
    long result;
    asm volatile("syscall" : "=a"(result) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return result;
}
[[maybe_unused, noreturn]] static void finish(long code) { call(60,code); __builtin_unreachable(); }
