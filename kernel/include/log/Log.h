
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Log

\************************************************************************/

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

// Debug out on COM2
#define LOG_COM_INDEX 1

#define LOG_DEBUG 0x0001
#define LOG_VERBOSE 0x0002
#define LOG_WARNING 0x0004
#define LOG_ERROR 0x0008
#define LOG_TEST 0x0010

#define INTERRUPT_LOG_SAMPLE_LIMIT 4

/***************************************************************************/

typedef struct tag_KERNEL_LOG_RECENT_VIEW {
    LPSTR Text;
    UINT TextBufferSize;
    UINT MaxLines;
    U32 Sequence;
    UINT TotalLines;
    UINT CopiedLines;
    BOOL Truncated;
} KERNEL_LOG_RECENT_VIEW, *LPKERNEL_LOG_RECENT_VIEW;

/***************************************************************************/

void InitKernelLog(void);
void KernelLogSetTagFilter(LPCSTR TagFilter);
LPCSTR KernelLogGetTagFilter(void);
U32 KernelLogGetRecentSequence(void);
BOOL KernelLogCaptureRecentLines(LPKERNEL_LOG_RECENT_VIEW View);
void KernelLogText(U32 Type, LPCSTR Format, ...);
void KernelLogTextFromFunction(U32 Type, LPCSTR FunctionName, LPCSTR Format, ...);
void KernelLogMem(U32 Type, LINEAR Memory, U32 Size);
void LogTaskSystemStructures(U32 Type);

/***************************************************************************/

#endif
