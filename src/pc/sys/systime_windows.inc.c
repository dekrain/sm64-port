#include "../systime.h"
#include <windows.h>

double sys_get_time_secs(void) {
    LARGE_INTEGER time, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&time);
    return (double)time.QuadPart / freq.QuadPart;
}

uint64_t sys_get_time_usec(void) {
    LARGE_INTEGER time, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&time);
    uint64_t qpc = time.QuadPart;
    uint64_t qpf = freq.QuadPart;
    return qpc / qpf * 1000000 + qpc % qpf * 1000000 / qpf;
}
