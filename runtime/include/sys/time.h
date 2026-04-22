#ifndef SYS_TIME_H_INCLUDED
#define SYS_TIME_H_INCLUDED

#include "types.h"

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

extern int gettimeofday(struct timeval* time_value, struct timezone* time_zone);

#endif
