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


    NVMe

\************************************************************************/

#ifndef DRIVERS_NVME_CORE_H_INCLUDED
#define DRIVERS_NVME_CORE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "drivers/bus/PCI.h"
#include "utils/Cooldown.h"

/************************************************************************/
// Macros

#define NVME_PCI_CLASS 0x01
#define NVME_PCI_SUBCLASS 0x08
#define NVME_PCI_PROG_IF 0x02

#define NVME_REG_CAP 0x00
#define NVME_REG_VS 0x08
#define NVME_REG_INTMS 0x0C
#define NVME_REG_INTMC 0x10
#define NVME_REG_CC 0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA 0x24
#define NVME_REG_ASQ 0x28
#define NVME_REG_ACQ 0x30

#define NVME_ADMIN_OP_CREATE_IO_SQ 0x01
#define NVME_ADMIN_OP_CREATE_IO_CQ 0x05
#define NVME_ADMIN_OP_IDENTIFY 0x06
#define NVME_ADMIN_OP_SET_FEATURES 0x09

#define NVME_IO_OP_NOOP 0x00
#define NVME_IO_OP_WRITE 0x01
#define NVME_IO_OP_READ 0x02

#define NVME_FEATURE_NUMBER_OF_QUEUES 0x07

#define NVME_CQ_FLAGS_PC (1 << 0)
#define NVME_CQ_FLAGS_IEN (1 << 1)
#define NVME_SQ_FLAGS_PC (1 << 0)

#define NVME_CC_EN 0x1
#define NVME_CC_CSS_SHIFT 4
#define NVME_CC_MPS_SHIFT 7
#define NVME_CC_AMS_SHIFT 11
#define NVME_CC_SHN_SHIFT 14
#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_IOCQES_SHIFT 20

/************************************************************************/
// Type definitions

typedef struct PACKED tag_NVME_COMMAND {
    U8 Opcode;
    U8 Flags;
    U16 CommandId;
    U32 NamespaceId;
    U32 Reserved0[2];
    U32 MetadataPointerLow;
    U32 MetadataPointerHigh;
    U32 Prp1Low;
    U32 Prp1High;
    U32 Prp2Low;
    U32 Prp2High;
    U32 CommandDword10;
    U32 CommandDword11;
    U32 CommandDword12;
    U32 CommandDword13;
    U32 CommandDword14;
    U32 CommandDword15;
} NVME_COMMAND, *LPNVME_COMMAND;

typedef struct PACKED tag_NVME_COMPLETION {
    U32 Result;
    U32 Reserved;
    U16 SubmissionQueueHead;
    U16 SubmissionQueueId;
    U16 CommandId;
    U16 Status;
} NVME_COMPLETION, *LPNVME_COMPLETION;

typedef struct tag_NVME_QUEUE_BUFFER {
    LINEAR Base;
    LPVOID Raw;
    PHYSICAL Physical;
    U32 Size;
} NVME_QUEUE_BUFFER, *LPNVME_QUEUE_BUFFER;

typedef struct tag_NVME_DEVICE {
    PCI_DEVICE_FIELDS

    LINEAR MmioBase;
    U32 MmioSize;

    NVME_QUEUE_BUFFER AdminSqBuffer;
    NVME_QUEUE_BUFFER AdminCqBuffer;
    U32 AdminSqEntries;
    U32 AdminCqEntries;
    U8* AdminSq;
    U8* AdminCq;
    UINT AdminSqTail;
    UINT AdminCqHead;
    U8 AdminCqPhase;
    U32 DoorbellStride;
    U8 InterruptSlot;
    U8 MsixVector;
    BOOL MsixEnabled;

    NVME_QUEUE_BUFFER IoSqBuffer;
    NVME_QUEUE_BUFFER IoCqBuffer;
    U32 IoSqEntries;
    U32 IoCqEntries;
    U8* IoSq;
    U8* IoCq;
    UINT IoSqTail;
    UINT IoCqHead;
    U8 IoCqPhase;
    U16 IoQueueId;
    U16 IoCommandId;
    U32 LogicalBlockSize;
    STR Serial[21];   // Controller serial string from Identify Controller
    STR Model[41];    // Controller model string from Identify Controller
    STR Firmware[9];  // Controller firmware string from Identify Controller
    COOLDOWN IoCompletionMismatchWarningCooldown;
    COOLDOWN IoCompletionTimeoutWarningCooldown;
    COOLDOWN IoCompletionCoherencyWarningCooldown;
    COOLDOWN AdminCompletionMismatchWarningCooldown;
    COOLDOWN AdminCompletionTimeoutWarningCooldown;
    DRIVER DiskDriver;
} NVME_DEVICE, *LPNVME_DEVICE;

/************************************************************************/

LPDRIVER NVMeGetDriver(void);

/************************************************************************/

#endif  // DRIVERS_NVME_CORE_H_INCLUDED
