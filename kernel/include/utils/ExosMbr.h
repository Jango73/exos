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


    EXOS MBR

\************************************************************************/
#ifndef EXOS_MBR_H_INCLUDED
#define EXOS_MBR_H_INCLUDED

/***************************************************************************/

#include "fs/Storage.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// EXOS MBR (EXFS boot record)

typedef struct tag_EXFSMBR {
    U8 Jump[4];          // Jump to code and 2 NOPs
    U8 OEMName[8];       // "EXOS    "
    U8 MediaDescriptor;  // 0xF8 for hard storage
    U8 LogicalDriveNumber;
    U16 Cylinders;
    U16 Heads;
    U16 SectorsPerTrack;
    U16 BytesPerSector;
    U16 SectorsPerCluster;
    U8 Code[486];
    U16 BIOSMark;  // 0xAA55
} EXFS_MBR, *LPEXFSMBR;

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

void ExosMbrFill(LPEXFSMBR Master, U16 SectorsPerCluster);
BOOL ExosMbrWrite(LPSTORAGE_UNIT Storage, U32 StartSector, U16 SectorsPerCluster);

/***************************************************************************/

#endif  // EXOS_MBR_H_INCLUDED
