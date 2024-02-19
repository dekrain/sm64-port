#include "../systime.h"
#include <time.h>

static struct timespec get_time(void) {
	struct timespec tp;
	static struct timespec last;
	if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0) {
		tp = last;
	} else {
		last = tp;
	}

	return tp;
}

double sys_get_time_secs(void) {
	struct timespec tp = get_time();
	return (double)tp.tv_sec + (double)tp.tv_nsec * 0.000000001;
}

uint64_t sys_get_time_usec(void) {
	struct timespec tp = get_time();
	return (uint64_t)tp.tv_sec * 1000000 + (uint64_t)tp.tv_nsec / 1000;
}
