#ifndef SYS_STAT_H_INCLUDED
#define SYS_STAT_H_INCLUDED

#include "types.h"

#define S_IFMT 0170000
#define S_IFREG 0100000

#define S_ISREG(Mode) (((Mode)&S_IFMT) == S_IFREG)

struct stat {
    mode_t st_mode;
    off_t st_size;
    time_t st_mtime;
};

extern int stat(const char* path, struct stat* info);

#endif
