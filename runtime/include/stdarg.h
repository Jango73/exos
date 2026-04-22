#ifndef STDARG_H_INCLUDED
#define STDARG_H_INCLUDED

#include "../../kernel/include/VarArg.h"

#ifdef __EXOS_32__
typedef VarArgList va_list;

#define va_start(AP, LAST) VarArgStart((AP), (LAST))
#define va_arg(AP, TYPE) VarArg((AP), TYPE)
#define va_end(AP) VarArgEnd((AP))
#define va_copy(DESTINATION, SOURCE) ((DESTINATION)[0] = (SOURCE)[0])
#else
typedef __builtin_va_list va_list;

#define va_start(AP, LAST) __builtin_va_start((AP), (LAST))
#define va_arg(AP, TYPE) __builtin_va_arg((AP), TYPE)
#define va_end(AP) __builtin_va_end((AP))
#define va_copy(DESTINATION, SOURCE) __builtin_va_copy((DESTINATION), (SOURCE))
#endif

#endif
