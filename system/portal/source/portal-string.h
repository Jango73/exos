/************************************************************************\

    EXOS Portal
    Copyright (c) 1999-2026 Jango73

    Portal string helpers

\************************************************************************/

#ifndef PORTAL_STRING_H_INCLUDED
#define PORTAL_STRING_H_INCLUDED

/************************************************************************/

#include "exos.h"
#include "exos-runtime-string.h"

/************************************************************************/

void StringClear(LPSTR Text);
void StringConcat(LPSTR Destination, LPCSTR Source);
INT StringCompare(LPCSTR Left, LPCSTR Right);
void U32ToHexString(U32 Value, LPSTR Text);

/************************************************************************/

#endif
