
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


    ID

\************************************************************************/

#ifndef ID_H_INCLUDED
#define ID_H_INCLUDED

/************************************************************************/
// Kernel object type identifiers

#define KOID_NONE 0x00000000
#define KOID_PROCESS 0x434F5250           // "PROC"
#define KOID_TASK 0x4B534154              // "TASK"
#define KOID_MUTEX 0x4D555458             // "MUTX"
#define KOID_SECURITY 0x55434553          // "SECU"
#define KOID_MESSAGE 0x4753534D           // "MSSG"
#define KOID_HEAP 0x50414548              // "HEAP"
#define KOID_DRIVER 0x52565244            // "DRVR"
#define KOID_PCIDEVICE 0x44494350         // "PCID"
#define KOID_USBDEVICE 0x44425355         // "USBD"
#define KOID_USBINTERFACE 0x49425355      // "USBI"
#define KOID_USBENDPOINT 0x45425355       // "USBE"
#define KOID_USBSTORAGE 0x534D5355        // "USMS"
#define KOID_DISK 0x4B534944              // "DISK"
#define KOID_IOCONTROL 0x54434F49         // "IOCT"
#define KOID_FILESYSTEM 0x53595346        // "FSYS"
#define KOID_FILE 0x454C4946              // "FILE"
#define KOID_GRAPHICSCONTEXT 0x43584647   // "GFXC"
#define KOID_DESKTOP 0x544B5344           // "DSKT"
#define KOID_WINDOW 0x444E4957            // "WIND"
#define KOID_WINDOW_CLASS 0x534C4357      // "WCLS"
#define KOID_BRUSH 0x48535242             // "BRSH"
#define KOID_PEN 0x5F4E4550               // "PEN_"
#define KOID_FONT 0x544E4F46              // "FONT"
#define KOID_BITMAP 0x504D5442            // "BTMP"
#define KOID_USER_ACCOUNT 0x52455355       // "USER"
#define KOID_USER_SESSION 0x53534553       // "SESS"
#define KOID_NETWORKDEVICE 0x4454454E     // "NETD"
#define KOID_SOCKET 0x4B434F53            // "SOCK"
#define KOID_ARP 0x5F505241               // "ARP_"
#define KOID_IPV4 0x34565049              // "IPV4"
#define KOID_UDP 0x5F504455               // "UDP_"
#define KOID_DHCP 0x50434844              // "DHCP"
#define KOID_TCP 0x5F504354               // "TCP_"
#define KOID_KERNELEVENT 0x544E5645       // "EVNT"
#define KOID_MEMORY_REGION_DESCRIPTOR 0x44524D56 // "VMRD"
#define KOID_EXECUTABLE_MODULE_IMAGE 0x49444F4D // "MODI"
#define KOID_EXECUTABLE_MODULE_BINDING 0x42444F4D // "MODB"

/************************************************************************/
// Known partition type identifiers

#define FSID_NONE 0x00
#define FSID_DOS_FAT12 0x01        // DOS 12-bit FAT
#define FSID_XENIXROOT 0x02        // XENIX root
#define FSID_XENIXUSER 0x03        // XENIX usr
#define FSID_DOS_FAT16S 0x04       // DOS 16-bit FAT smaller than 32 MB
#define FSID_EXTENDED 0x05         // Extended Partition
#define FSID_DOS_FAT16L 0x06       // DOS 16-bit FAT larger than 32 MB
#define FSID_OS2_HPFS 0x07         // OS2 HPFS, NTFS
#define FSID_DOS_AIX 0x08          // DOS, AIX
#define FSID_DOS_AIX_BOOT 0x09     // DOS, AIX bootable
#define FSID_OS2_BOOTMAN 0x0A      // OS2 Boot Manager
#define FSID_DOS_FAT32 0x0B        // DOS 32-bit FAT
#define FSID_DOS_FAT32_LBA1 0x0C   // DOS 32-bit FAT using LBA1 Extensions
#define FSID_DOS_FAT16L_LBA1 0x0E  // DOS 16-bit FAT using LBA1 Extensions
#define FSID_DOS_FAT32X 0x0F       // DOS 32-bit FAT
#define FSID_OPUS 0x10             // OPUS
#define FSID_HIDDEN_DOS_FAT12 0x11
#define FSID_HIDDEN_IFS 0x17
#define FSID_NEC_DOS_3X 0x24
#define FSID_NOS 0x32
#define FSID_THEOS 0x38
#define FSID_VENIX 0x40            // Venix 80286
#define FSID_POWERPC 0x41          // PowerPC
#define FSID_SFS 0x42              // Secure Filesystem
#define FSID_GOBACK 0x44           // GoBack partition
#define FSID_BOOT_US 0x45          // Boot-US boot manager
#define FSID_ADAOS 0x4A            // AdaOS Aquila
#define FSID_OBERON 0x4C           // Oberon partition
#define FSID_NOVELL 0x51           // Novell
#define FSID_MICROPORT 0x52        // Microport
#define FSID_EZDRIVE 0x55          // EZ-Drive
#define FSID_UNIX_V 0x63           // Unix System V
#define FSID_NOVELL_NET1 0x64      // Novell Netware
#define FSID_NOVELL_NET2 0x65      // Novell Netware
#define FSID_PC_IX 0x75            // PC/IX
#define FSID_XOSL 0x78             // XOSL FS
#define FSID_OLD_MINIX 0x80        // Old Minix
#define FSID_LINUXMINIX 0x81       // Linux / Minix
#define FSID_LINUXSWAP 0x82        // Linux Swap
#define FSID_LINUXNATIVE 0x83      // Linux Native
#define FSID_LINUX_EXT2 0x83       // Linux EXT2
#define FSID_GPT_PROTECTIVE 0xEE   // GPT protective MBR
#define FSID_LINUX_EXT3 0x83       // Linux EXT3
#define FSID_LINUX_EXT4 0x83       // Linux EXT4
#define FSID_LINUX_EXTENDED 0x85   // Linux Extended
#define FSID_LINUX_LVM 0x8E        // Linux LVM
#define FSID_AMOEBA 0x93           // Amoeba
#define FSID_BSD386 0xA5           // BSD 386
#define FSID_MACOS_X 0xA8          // Mac-OS X
#define FSID_NETBSD 0xA9           // NetBSD
#define FSID_BEOS 0xEB             // BeOS
#define FSID_DOS_SECOND 0xF2       // DOS Secondary
#define FSID_EXOS 0xF8             // EXOS
#define FSID_XENIX_BBT 0xFF        // Xenix Bad Block Table

/************************************************************************/
// GPT partition type GUIDs (little-endian on disk)

#define GPT_GUID_LENGTH 16
#define GPT_GUID_LINUX_EXTX { \
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, \
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4  \
}
#define GPT_GUID_EFI_SYSTEM { \
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, \
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B  \
}
#define GPT_GUID_MICROSOFT_BASIC_DATA { \
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, \
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7  \
}

/************************************************************************/

#endif  // ID_H_INCLUDED
