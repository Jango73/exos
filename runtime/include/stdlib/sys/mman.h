#ifndef SYS_MMAN_H_INCLUDED
#define SYS_MMAN_H_INCLUDED

#include "types.h"

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_FAILED ((void*)-1)

extern void* mmap(void* address, size_t length, int protection, int flags, int file_descriptor, off_t offset);
extern int munmap(void* address, size_t length);
extern int mprotect(void* address, size_t length, int protection);

#endif
