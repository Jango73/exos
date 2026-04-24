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


    Pipe

\************************************************************************/

#ifndef PIPE_H_INCLUDED
#define PIPE_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#define PIPE_ENDPOINT_DIRECTION_READ 0x00000001
#define PIPE_ENDPOINT_DIRECTION_WRITE 0x00000002

/************************************************************************/

UINT PipeCreateHandles(HANDLE* OutReadHandle, HANDLE* OutWriteHandle);
UINT PipeReadEndpoint(LPVOID EndpointObject, LPVOID Buffer, UINT Count);
UINT PipeWriteEndpoint(LPVOID EndpointObject, LPCVOID Buffer, UINT Count);
UINT PipeCloseEndpoint(LPVOID EndpointObject);
U32 PipeGetEndpointDirection(LPVOID EndpointObject);

/************************************************************************/

#endif  // PIPE_H_INCLUDED
