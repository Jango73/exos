#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

#include "sys/types.h"

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

extern time_t time(time_t* value);
extern struct tm* localtime(const time_t* value);

#endif
