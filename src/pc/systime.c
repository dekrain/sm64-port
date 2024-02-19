#if defined(__linux__)
#include "sys/systime_linux.inc.c"
#elif defined(_WIN32) || defined(_WIN64)
#include "sys/systime_windows.inc.c"
#else
double sys_get_time_secs(void) {
	return 0.0;
}

uint64_t sys_get_time_usec(void) {
	return 0;
}
#endif
