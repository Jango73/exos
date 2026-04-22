/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime String Helpers

\************************************************************************/

#ifndef EXOS_STRING_H_INCLUDED
#define EXOS_STRING_H_INCLUDED

/************************************************************************/

#include "exos-runtime.h"

/************************************************************************/

#define PF_ZEROPAD 1
#define PF_SIGN 2
#define PF_PLUS 4
#define PF_SPACE 8
#define PF_LEFT 16
#define PF_SPECIAL 32
#define PF_LARGE 64

/************************************************************************/

UINT StringLength(LPCSTR Src);
void StringCopy(LPSTR Dst, LPCSTR Src);
void StringCopyLimit(LPSTR Dst, LPCSTR Src, UINT MaxLength);
void U32ToString(U32 Number, LPSTR Text);
void StringPrintFormatArgs(LPSTR Destination, LPCSTR Format, VarArgList Args);

/************************************************************************/

#endif
