
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    Driver getters

\************************************************************************/

#ifndef DRIVERGETTERS_H_INCLUDED
#define DRIVERGETTERS_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/************************************************************************/
// External symbols

LPDRIVER ConsoleGetDriver(void);
LPDRIVER KernelLogGetDriver(void);
LPDRIVER MemoryManagerGetDriver(void);
LPDRIVER TaskSegmentsGetDriver(void);
LPDRIVER InterruptsGetDriver(void);
LPDRIVER KernelProcessGetDriver(void);
LPDRIVER ACPIGetDriver(void);
LPDRIVER LocalAPICGetDriver(void);
LPDRIVER IOAPICGetDriver(void);
LPDRIVER InterruptControllerGetDriver(void);
LPDRIVER DeviceInterruptGetDriver(void);
LPDRIVER DeferredWorkGetDriver(void);
LPDRIVER StdKeyboardGetDriver(void);
LPDRIVER ClockGetDriver(void);
LPDRIVER PCIGetDriver(void);
LPDRIVER ATAStorageGetDriver(void);
LPDRIVER SATAStorageGetDriver(void);
LPDRIVER AHCIPCIGetDriver(void);
LPDRIVER NVMeGetDriver(void);
LPDRIVER E1000GetDriver(void);
LPDRIVER RTL8139GetDriver(void);
LPDRIVER RTL8139CPlusGetDriver(void);
LPDRIVER RTL8169GetDriver(void);
LPDRIVER RAMStorageGetDriver(void);
LPDRIVER USBStorageGetDriver(void);
LPDRIVER XHCIGetDriver(void);
LPDRIVER FileSystemGetDriver(void);
LPDRIVER NetworkManagerGetDriver(void);
LPDRIVER UserAccountGetDriver(void);
LPDRIVER GraphicsSelectorGetDriver(void);
BOOL GraphicsSelectorForceBackendByName(LPCSTR Name);
LPCSTR GraphicsSelectorGetActiveBackendName(void);
LPDRIVER GraphicsSelectorGetActiveBackendDriver(void);
LPDRIVER VGAGetDriver(void);
LPDRIVER GOPGetDriver(void);
LPDRIVER IntelGfxGetDriver(void);
LPDRIVER VESAGetDriver(void);
LPDRIVER EXFSGetDriver(void);

/************************************************************************/

#endif  // DRIVERGETTERS_H_INCLUDED
