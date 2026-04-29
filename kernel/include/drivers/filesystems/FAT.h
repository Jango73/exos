
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


    FAT

\************************************************************************/
#ifndef FAT_H_INCLUDED
#define FAT_H_INCLUDED

/***************************************************************************/

#include "core/ID.h"
#include "fs/File-System.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

// The FAT16 Master Boot Record
// Code begins at 0x005A

typedef struct tag_FAT16MBR {
    U8 Jump[3];
    U8 OEMName[8];
    U16 BytesPerSector;
    U8 SectorsPerCluster;
    U16 ReservedSectors;
    U8 NumFATs;
    U16 NumRootEntries;
    U16 NumSectors_Less32MB;
    U8 MediaDescriptor;  // 0xF8 for Hard Disks
    U16 SectorsPerFAT;
    U16 SectorsPerTrack;
    U16 NumHeads;          // Number of heads of media
    U32 NumHiddenSectors;  // Number of hidden sectors in partition
    U32 NumSectors;        // Number of sectors in partition
    U16 LogDriveNumber;
    U8 ExtendedSignature;
    U32 SerialNumber;
    U8 VolumeName[11];
    U8 FATName[8];
    U8 Code[448];
    U16 BIOSMark;  // 0xAA55
} FAT16MBR, *LPFAT16MBR;

/***************************************************************************/

// The FAT32 Master Boot Record
// Code begins at 0x005A

typedef struct tag_FAT32MBR {
    U8 Jump[3];
    U8 OEMName[8];
    U16 BytesPerSector;
    U8 SectorsPerCluster;
    U16 ReservedSectors;
    U8 NumFATs;
    U16 NumRootEntries_NA;  // Not available for FAT32
    U16 NumSectors_NA;      // Not available for FAT32
    U8 MediaDescriptor;     // 0xF8 for Hard Disks
    U16 SectorsPerFAT_NA;   // Not available for FAT32
    U16 SectorsPerTrack;
    U16 NumHeads;          // Number of heads of media
    U32 NumHiddenSectors;  // Number of hidden sectors in partition
    U32 NumSectors;        // Number of sectors in partition
    U32 NumSectorsPerFAT;
    U16 Flags;
    U16 Version;
    U32 RootCluster;
    U16 InfoSector;
    U16 BackupBootSector;
    U8 Reserved1[12];
    U8 LogicalDriveNumber;
    U8 Reserved2;
    U8 ExtendedSignature;
    U32 SerialNumber;
    U8 VolumeName[11];  // Unused, volume name is in root
    U8 FATName[8];      // "FAT32"
    U8 Code[420];
    U16 BIOSMark;  // 0xAA55
} FAT32MBR, *LPFAT32MBR;

/***************************************************************************/

typedef struct tag_FATDIRENTRY {
    STR Name[8];
    STR Ext[3];
    U8 Attributes;
    U8 Unused[10];
    U16 Time;
    U16 Date;
    U16 Cluster;
    U32 Size;
} FATDIRENTRY, *LPFATDIRENTRY;

typedef struct tag_FATDIRENTRY_EXT {
    STR Name[8];
    STR Ext[3];
    U8 Attributes;
    U8 NT;
    U8 CreationMS;
    U16 CreationHM;
    U16 CreationYM;
    U16 LastAccessDate;
    U16 ClusterHigh;
    U16 Time;
    U16 Date;
    U16 ClusterLow;
    U32 Size;
} FATDIRENTRY_EXT, *LPFATDIRENTRY_EXT;

typedef struct tag_FATDIRENTRY_LFN {
    U8 Ordinal;
    USTR Char01;
    USTR Char02;
    USTR Char03;
    USTR Char04;
    USTR Char05;
    U8 Attributes;
    U8 Type;  // Always 0
    U8 Checksum;
    USTR Char06;
    USTR Char07;
    USTR Char08;
    USTR Char09;
    USTR Char10;
    USTR Char11;
    U16 Cluster;
    USTR Char12;
    USTR Char13;
} FATDIRENTRY_LFN, *LPFATDIRENTRY_LFN;

#define FAT_ATTR_READONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME 0x08
#define FAT_ATTR_FOLDER 0x10
#define FAT_ATTR_ARCHIVE 0x20

#define FAT_ATTR_LFN (FAT_ATTR_READONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME)

/***************************************************************************/

#define FAT16_CLUSTER_AVAIL ((U16)0x0000)
#define FAT16_CLUSTER_RESERVED ((U16)0xFFF0)
#define FAT16_CLUSTER_BAD ((U16)0xFFF7)
#define FAT16_CLUSTER_LAST ((U16)0xFFFF)

#define FAT32_CLUSTER_AVAIL ((U32)0x00000000)
#define FAT32_CLUSTER_RESERVED ((U32)0xFFFFFFF0)
#define FAT32_CLUSTER_BAD ((U32)0xFFFFFFF7)
#define FAT32_CLUSTER_LAST ((U32)0xFFFFFFFF)

/***************************************************************************/

#define FAT_DATE_DAY_MASK (BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4)
#define FAT_DATE_DAY_SHFT 0

#define FAT_DATE_MONTH_MASK (BIT_5 | BIT_6 | BIT_7 | BIT_8)
#define FAT_DATE_MONTH_SHFT 5

#define FAT_DATE_YEAR_MASK (BIT_9 | BIT_10 | BIT_11 | BIT_12 | BIT_13 | BIT_14 | BIT_15)
#define FAT_DATE_YEAR_SHFT 9

#define FAT_TIME_SECOND_MASK (BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4)
#define FAT_TIME_SECOND_SHFT 0

#define FAT_TIME_MINUTE_MASK (BIT_5 | BIT_6 | BIT_7 | BIT_8 | BIT_9 | BIT_10)
#define FAT_TIME_MINUTE_SHFT 5

#define FAT_TIME_HOUR_MASK (BIT_11 | BIT_12 | BIT_13 | BIT_14 | BIT_15)
#define FAT_TIME_HOUR_SHFT 11

/***************************************************************************/

typedef struct tag_FATFILELOC {
    U32 PreviousCluster;
    U32 FolderCluster;
    U32 FileCluster;
    U32 DataCluster;
    U32 Offset;
} FATFILELOC, *LPFATFILELOC;

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

/**
 * @brief Read the boot sector of a FAT partition and validate the BIOS mark.
 *
 * @param Disk Physical disk hosting the partition.
 * @param Partition Partition descriptor.
 * @param Base Base sector offset.
 * @param Buffer Caller-provided SECTOR_SIZE buffer to fill.
 * @return TRUE if the sector is read successfully and the BIOS mark is valid.
 */
BOOL FATReadBootSector(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, LPVOID Buffer);

#endif
