
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


    xHCI (internal)

\************************************************************************/

#ifndef XHCI_INTERNAL_H_INCLUDED
#define XHCI_INTERNAL_H_INCLUDED

#include "Base.h"
#include "User.h"
#include "core/DriverEnum.h"
#include "core/Kernel.h"
#include "core/KernelData.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "drivers/usb/USB.h"
#include "drivers/usb/XHCI.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "sync/DeferredWork.h"
#include "text/CoreString.h"
#include "utils/FailureGate.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// xHCI capability registers

#define XHCI_CAPLENGTH 0x00
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCSPARAMS3 0x0C
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
#define XHCI_HCCPARAMS2 0x1C

#define XHCI_HCSPARAMS1_MAXSLOTS_MASK 0x000000FF
#define XHCI_HCSPARAMS1_MAXINTRS_MASK 0x0007FF00
#define XHCI_HCSPARAMS1_MAXINTRS_SHIFT 8
#define XHCI_HCSPARAMS1_MAXPORTS_MASK 0xFF000000
#define XHCI_HCSPARAMS1_MAXPORTS_SHIFT 24
#define XHCI_HCSPARAMS1_PPC 0x00000010
#define XHCI_HCSPARAMS2_SCRATCHPAD_LOW_MASK 0xF8000000
#define XHCI_HCSPARAMS2_SCRATCHPAD_LOW_SHIFT 27
#define XHCI_HCSPARAMS2_SCRATCHPAD_HIGH_MASK 0x03E00000
#define XHCI_HCSPARAMS2_SCRATCHPAD_HIGH_SHIFT 21

#define XHCI_HCCPARAMS1_AC64 0x00000001
#define XHCI_HCCPARAMS1_CSZ 0x00000004

/************************************************************************/
// xHCI operational registers (offset from operational base)

#define XHCI_OP_USBCMD 0x00
#define XHCI_OP_USBSTS 0x04
#define XHCI_OP_PAGESIZE 0x08
#define XHCI_OP_DNCTRL 0x14
#define XHCI_OP_CRCR 0x18
#define XHCI_OP_DCBAAP 0x30
#define XHCI_OP_CONFIG 0x38

#define XHCI_USBCMD_RS 0x00000001
#define XHCI_USBCMD_HCRST 0x00000002
#define XHCI_USBCMD_INTE 0x00000004

#define XHCI_USBSTS_HCH 0x00000001
#define XHCI_USBSTS_CNR 0x00000800

/************************************************************************/
// xHCI port registers (offset from operational base)

#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC_STRIDE 0x10

#define XHCI_PORTSC_CCS 0x00000001
#define XHCI_PORTSC_PED 0x00000002
#define XHCI_PORTSC_PR 0x00000010
#define XHCI_PORTSC_PP 0x00000200
#define XHCI_PORTSC_PLS_MASK 0x000001E0
#define XHCI_PORTSC_SPEED_MASK 0x00003C00
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_W1C_MASK 0x00FE0000
#define XHCI_ROOT_PORT_PROBE_FAILURE_THRESHOLD 8

#define XHCI_ENUM_ERROR_NONE 0u
#define XHCI_ENUM_ERROR_BUSY 1u
#define XHCI_ENUM_ERROR_RESET_TIMEOUT 2u
#define XHCI_ENUM_ERROR_INVALID_SPEED 3u
#define XHCI_ENUM_ERROR_INIT_STATE 4u
#define XHCI_ENUM_ERROR_ENABLE_SLOT 5u
#define XHCI_ENUM_ERROR_ADDRESS_DEVICE 6u
#define XHCI_ENUM_ERROR_DEVICE_DESC 7u
#define XHCI_ENUM_ERROR_CONFIG_DESC 8u
#define XHCI_ENUM_ERROR_CONFIG_PARSE 9u
#define XHCI_ENUM_ERROR_SET_CONFIG 10u
#define XHCI_ENUM_ERROR_HUB_INIT 11u
#define XHCI_ENUM_ERROR_BLACKLISTED 12u

/************************************************************************/
// xHCI runtime registers

#define XHCI_RT_MFINDEX 0x00
#define XHCI_RT_INTERRUPTER_BASE 0x20
#define XHCI_RT_INTERRUPTER_STRIDE 0x20

#define XHCI_IMAN 0x00
#define XHCI_IMOD 0x04
#define XHCI_ERSTSZ 0x08
#define XHCI_ERSTBA 0x10
#define XHCI_ERDP 0x18
#define XHCI_ERDP_EHB 0x00000008

#define XHCI_IMAN_IP 0x00000001
#define XHCI_IMAN_IE 0x00000002

/************************************************************************/
// xHCI doorbell registers

#define XHCI_DOORBELL_TARGET_MASK 0x000000FF

/************************************************************************/
// TRB definitions

#define XHCI_TRB_TYPE_SHIFT 10
#define XHCI_TRB_TYPE_LINK 6
#define XHCI_TRB_TYPE_MASK 0x3F
#define XHCI_TRB_TRT_SHIFT 16
#define XHCI_TRB_TRT_NO_DATA 0
#define XHCI_TRB_TRT_OUT_DATA 2
#define XHCI_TRB_TRT_IN_DATA 3

#define XHCI_TRB_CYCLE 0x00000001
#define XHCI_TRB_TOGGLE_CYCLE 0x00000002
#define XHCI_TRB_ISP 0x00000004
#define XHCI_TRB_IOC 0x00000020
#define XHCI_TRB_IDT 0x00000040
#define XHCI_TRB_DIR_IN 0x00010000

#define XHCI_TRB_TYPE_NORMAL 1
#define XHCI_TRB_TYPE_SETUP_STAGE 2
#define XHCI_TRB_TYPE_DATA_STAGE 3
#define XHCI_TRB_TYPE_STATUS_STAGE 4
#define XHCI_TRB_TYPE_ENABLE_SLOT 9
#define XHCI_TRB_TYPE_DISABLE_SLOT 0x0A
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13
#define XHCI_TRB_TYPE_RESET_ENDPOINT 0x0E
#define XHCI_TRB_TYPE_STOP_ENDPOINT 0x0F
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER 0x10
#define XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT 33
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32

#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_COMPLETION_STALL_ERROR 6
#define XHCI_COMPLETION_SHORT_PACKET 13
#define XHCI_COMPLETION_CONTEXT_STATE_ERROR 19

#define XHCI_EP0_DCI 1

#define XHCI_COMMAND_RING_TRBS 256
#define XHCI_EVENT_RING_TRBS 256
#define XHCI_TRANSFER_RING_TRBS 256

/************************************************************************/

#define XHCI_COMPLETION_QUEUE_MAX 64

#define XHCI_ENDPOINT_STATE_DISABLED 0
#define XHCI_ENDPOINT_STATE_RUNNING 1
#define XHCI_ENDPOINT_STATE_HALTED 2
#define XHCI_ENDPOINT_STATE_STOPPED 3
#define XHCI_ENDPOINT_STATE_ERROR 4

#define XHCI_SLOT_CTX_ROUTE_STRING_MASK 0x000FFFFF
#define XHCI_SLOT_CTX_SPEED_SHIFT 20
#define XHCI_SLOT_CTX_MTT 0x02000000
#define XHCI_SLOT_CTX_HUB 0x04000000
#define XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT 27

#define XHCI_SLOT_CTX_ROOT_PORT_SHIFT 16
#define XHCI_SLOT_CTX_PORT_COUNT_SHIFT 24

#define XHCI_SLOT_CTX_TT_HUB_SLOT_SHIFT 0
#define XHCI_SLOT_CTX_TT_PORT_SHIFT 8

#define USB_HUB_PORT_STATUS_CONNECTION 0x0001
#define USB_HUB_PORT_STATUS_ENABLE 0x0002
#define USB_HUB_PORT_STATUS_RESET 0x0010
#define USB_HUB_PORT_STATUS_POWER 0x0100
#define USB_HUB_PORT_STATUS_LOW_SPEED 0x0200
#define USB_HUB_PORT_STATUS_HIGH_SPEED 0x0400

#define USB_HUB_PORT_CHANGE_CONNECTION 0x0001
#define USB_HUB_PORT_CHANGE_ENABLE 0x0002
#define USB_HUB_PORT_CHANGE_RESET 0x0010

/************************************************************************/

typedef struct tag_XHCI_TRB {
    U32 Dword0;
    U32 Dword1;
    U32 Dword2;
    U32 Dword3;
} XHCI_TRB, *LPXHCI_TRB;

typedef struct tag_XHCI_CONTEXT_32 {
    U32 Dword0;
    U32 Dword1;
    U32 Dword2;
    U32 Dword3;
    U32 Dword4;
    U32 Dword5;
    U32 Dword6;
    U32 Dword7;
} XHCI_CONTEXT_32, *LPXHCI_CONTEXT_32;

typedef struct tag_XHCI_USB_ENDPOINT {
    LISTNODE_FIELDS
    U8 Address;
    U8 Attributes;
    U16 MaxPacketSize;
    U8 Interval;
    U8 MaximumBurst;
    U8 CompanionAttributes;
    U16 CompanionBytesPerInterval;
    BOOL HasSuperSpeedCompanion;
    U8 Dci;
    PHYSICAL TransferRingPhysical;
    LINEAR TransferRingLinear;
    U32 TransferRingCycleState;
    U32 TransferRingEnqueueIndex;
} XHCI_USB_ENDPOINT, *LPXHCI_USB_ENDPOINT;

typedef struct tag_XHCI_USB_INTERFACE {
    LISTNODE_FIELDS
    U8 ConfigurationValue;
    U8 Number;
    U8 AlternateSetting;
    U8 NumEndpoints;
    U8 InterfaceClass;
    U8 InterfaceSubClass;
    U8 InterfaceProtocol;
    U8 InterfaceIndex;
    UINT EndpointCount;
} XHCI_USB_INTERFACE, *LPXHCI_USB_INTERFACE;

typedef struct tag_XHCI_USB_CONFIGURATION {
    U8 ConfigurationValue;
    U8 ConfigurationIndex;
    U8 Attributes;
    U8 MaxPower;
    U8 NumInterfaces;
    U16 TotalLength;
    UINT InterfaceCount;
} XHCI_USB_CONFIGURATION, *LPXHCI_USB_CONFIGURATION;

typedef struct tag_XHCI_ERST_ENTRY {
    U64 SegmentBase;
    U16 SegmentSize;
    U16 Reserved;
    U32 Reserved2;
} XHCI_ERST_ENTRY, *LPXHCI_ERST_ENTRY;

typedef struct tag_XHCI_USB_DEVICE {
    USB_DEVICE_FIELDS
    BOOL Present;
    BOOL DestroyPending;
    U8 LastEnumError;
    U16 LastEnumCompletion;
    U32 LastRootPortProbeSignature;
    FAILURE_GATE RootPortFailureGate;
    U8 PortNumber;
    U8 RootPortNumber;
    U8 Depth;
    U8 SlotId;
    UINT ConfigCount;
    LPXHCI_USB_CONFIGURATION Configs;
    PHYSICAL InputContextPhysical;
    LINEAR InputContextLinear;
    PHYSICAL DeviceContextPhysical;
    LINEAR DeviceContextLinear;
    PHYSICAL TransferRingPhysical;
    LINEAR TransferRingLinear;
    U32 TransferRingCycleState;
    U32 TransferRingEnqueueIndex;

    BOOL IsHub;
    U8 HubPortCount;
    struct tag_XHCI_USB_DEVICE** HubChildren;
    U16* HubPortStatus;
    LPXHCI_USB_ENDPOINT HubInterruptEndpoint;
    U16 HubInterruptLength;
    PHYSICAL HubStatusPhysical;
    LINEAR HubStatusLinear;
    U64 HubStatusTrbPhysical;
    BOOL HubStatusPending;
    U32 RouteString;
    U8 ParentPort;
    BOOL IsRootPort;
    struct tag_XHCI_DEVICE* Controller;
} XHCI_USB_DEVICE, *LPXHCI_USB_DEVICE;

typedef struct tag_XHCI_COMPLETION {
    U64 TrbPhysical;
    U32 Completion;
    U32 TransferLength;
    U8 Type;
    U8 SlotId;
    U8 EndpointId;
} XHCI_COMPLETION, *LPXHCI_COMPLETION;

struct tag_XHCI_DEVICE {
    PCI_DEVICE_FIELDS

    LINEAR MmioBase;
    U32 MmioSize;

    U8 CapLength;
    U16 HciVersion;
    U8 MaxSlots;
    U8 MaxPorts;
    U16 MaxInterrupters;
    U16 MaxScratchpadBuffers;
    U32 HccParams1;
    U32 HcsParams2;
    U32 ContextSize;

    LINEAR OpBase;
    LINEAR RuntimeBase;
    LINEAR DoorbellBase;

    PHYSICAL DcbaaPhysical;
    LINEAR DcbaaLinear;
    PHYSICAL ScratchpadArrayPhysical;
    LINEAR ScratchpadArrayLinear;
    PHYSICAL* ScratchpadPages;

    PHYSICAL CommandRingPhysical;
    LINEAR CommandRingLinear;
    U32 CommandRingCycleState;
    U32 CommandRingEnqueueIndex;

    PHYSICAL EventRingPhysical;
    LINEAR EventRingLinear;
    PHYSICAL EventRingTablePhysical;
    LINEAR EventRingTableLinear;

    U32 EventRingDequeueIndex;
    U32 EventRingCycleState;

    LPXHCI_USB_DEVICE* UsbDevices;
    U32 CommandTimeoutMS;
    U32 TransferTimeoutMS;

    XHCI_COMPLETION CompletionQueue[XHCI_COMPLETION_QUEUE_MAX];
    U32 CompletionCount;
    DEFERRED_WORK_TOKEN HubPollToken;

    U8 InterruptSlot;
    BOOL InterruptRegistered;
    BOOL InterruptEnabled;
    U32 InterruptCount;
    U32 LastObservedUsbStatus;
    BOOL HseTransitionLogged;
};

/************************************************************************/

#pragma pack(pop)

/************************************************************************/

// External symbols
U32 XHCI_Read32(LINEAR Base, U32 Offset);
void XHCI_Write32(LINEAR Base, U32 Offset, U32 Value);
void XHCI_Write64(LINEAR Base, U32 Offset, U64 Value);
LPXHCI_CONTEXT_32 XHCI_GetContextPointer(LINEAR Base, U32 ContextSize, U32 Index);
LINEAR XHCI_GetInterrupterBase(LPXHCI_DEVICE Device);
void XHCI_RingDoorbell(LPXHCI_DEVICE Device, U32 DoorbellIndex, U32 Target);
void XHCI_InitUsbDeviceObject(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_PopCompletion(
    LPXHCI_DEVICE Device, U8 Type, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut, U32* TransferLengthOut);
void XHCI_ClearTransferCompletions(LPXHCI_DEVICE Device, U8 SlotId, U8 EndpointId);
BOOL XHCI_PollForCompletion(
    LPXHCI_DEVICE Device, U8 Type, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut, U32* TransferLengthOut);
BOOL XHCI_CommandRingEnqueue(LPXHCI_DEVICE Device, const XHCI_TRB* Trb, U64* PhysicalOut);
BOOL XHCI_TransferRingEnqueue(LPXHCI_USB_DEVICE UsbDevice, const XHCI_TRB* Trb, U64* PhysicalOut);
BOOL XHCI_RingEnqueue(
    LINEAR RingLinear, PHYSICAL RingPhysical, U32* EnqueueIndex, U32* CycleState, U32 RingTrbs, const XHCI_TRB* Trb,
    U64* PhysicalOut);
BOOL XHCI_DequeueEvent(LPXHCI_DEVICE Device, XHCI_TRB* EventOut);
void XHCI_PollCompletions(LPXHCI_DEVICE Device);
void XHCI_LogHseTransitionIfNeeded(LPXHCI_DEVICE Device, LPCSTR Source);
BOOL XHCI_WaitForRegister(LINEAR Base, U32 Offset, U32 Mask, U32 Value, U32 Timeout, LPCSTR Name);
BOOL XHCI_AllocPage(LPCSTR Tag, PHYSICAL* PhysicalOut, LINEAR* LinearOut);
void XHCI_FreeResources(LPXHCI_DEVICE Device);
U32 XHCI_ReadPortStatus(LPXHCI_DEVICE Device, U32 PortIndex);
void XHCI_AddDeviceToList(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);

void XHCI_DestroyUsbDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, BOOL FreeSelf);
void XHCI_ReferenceUsbDevice(LPXHCI_USB_DEVICE UsbDevice);
void XHCI_ReleaseUsbDevice(LPXHCI_USB_DEVICE UsbDevice);
void XHCI_ReferenceUsbInterface(LPXHCI_USB_INTERFACE Interface);
void XHCI_ReleaseUsbInterface(LPXHCI_USB_INTERFACE Interface);
void XHCI_ReferenceUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint);
void XHCI_ReleaseUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint);
LPCSTR XHCI_SpeedToString(U32 SpeedId);
LPXHCI_USB_ENDPOINT XHCI_FindHubInterruptEndpoint(LPXHCI_USB_DEVICE UsbDevice);
LPXHCI_USB_ENDPOINT XHCI_FindInterfaceEndpoint(LPXHCI_USB_INTERFACE Interface, U8 EndpointType, BOOL DirectionIn);
BOOL XHCI_AddInterruptEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint);
BOOL XHCI_AddBulkEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint);
BOOL XHCI_AddBulkEndpointPair(
    LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT BulkOutEndpoint,
    LPXHCI_USB_ENDPOINT BulkInEndpoint);
BOOL XHCI_SubmitNormalTransfer(
    LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint, PHYSICAL BufferPhysical,
    U32 Length, BOOL InterruptOnShortPacket, U64* TrbPhysicalOut);
BOOL XHCI_UpdateHubSlotContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_ControlTransfer(
    LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, const USB_SETUP_PACKET* Setup, PHYSICAL Physical, LPVOID Linear,
    U16 Length, BOOL DirectionIn);
BOOL XHCI_ResetTransferEndpoint(
    LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint, BOOL EndpointHalted);
LPXHCI_USB_CONFIGURATION XHCI_GetSelectedConfig(LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_EnumerateDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
void XHCI_EnsureUsbDevices(LPXHCI_DEVICE Device);

BOOL XHCI_InitHub(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub);
void XHCI_RegisterHubPoll(LPXHCI_DEVICE Device);
BOOL XHCI_CheckTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut);
BOOL XHCI_CheckTransferCompletionRouted(
    LPXHCI_DEVICE Device, U64 TrbPhysical, U8 SlotId, U8 EndpointId, U32* CompletionOut, U32* TransferLengthOut,
    BOOL* UsedRouteFallbackOut, U64* ObservedTrbPhysicalOut);

U32 XHCI_EnumNext(LPDRIVER_ENUM_NEXT Next);
U32 XHCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty);

#endif  // XHCI_INTERNAL_H_INCLUDED
