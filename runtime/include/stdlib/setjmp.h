#ifndef SETJMP_H_INCLUDED
#define SETJMP_H_INCLUDED

typedef void* jmp_buf[5];

#define setjmp(ENVIRONMENT) __builtin_setjmp(ENVIRONMENT)
extern void longjmp(jmp_buf environment, int value);

#endif
