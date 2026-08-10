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


    VGA

\************************************************************************/

#include "drivers/graphics/vga/VGA.h"

#include "Arch.h"
#include "text/CoreString.h"
#include "GFX.h"
#include "system/System.h"

/***************************************************************************/

#define VGA_VER_MAJOR 1
#define VGA_VER_MINOR 0

/***************************************************************************/

#define VGA_ATTR 0x03C0
#define VGA_MISC 0x03C2
#define VGA_ENAB 0x03C3
#define VGA_SEQ 0x03C4
#define VGA_GFX 0x03CE
#define VGA_CRTC 0x03D4
#define VGA_STAT 0x03DA
#define VGA_TEXT_MEMORY 0xB8000

/***************************************************************************/

#define REGOFS_SEQ 0
#define REGOFS_MISC 5
#define REGOFS_CRTC 6
#define REGOFS_ATTR 31
#define REGOFS_GFX 51

/***************************************************************************/

static UINT VGACommands(UINT Function, UINT Parameter);
static U8 VGAReadCRTCRegister(U8 RegisterIndex);
static void VGAWriteCRTCRegister(U8 RegisterIndex, U8 Value);
static BOOL VGAReadCurrentTextModeInfo(LPVGAMODEINFO Info);
static BOOL VGARefreshTextContext(void);
static U16 VGAComposeTextCell(STR Character, U32 ForegroundColorIndex, U32 BackgroundColorIndex);
static U32 VGATextPutCell(LPGFX_TEXT_CELL_INFO Info);
static U32 VGATextClearRegion(LPGFX_TEXT_REGION_INFO Info);
static U32 VGATextScrollRegion(LPGFX_TEXT_REGION_INFO Info);
static U32 VGATextSetCursor(LPGFX_TEXT_CURSOR_INFO Info);
static U32 VGATextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info);
static UINT VGASetModeFromRequest(LPGRAPHICS_MODE_INFO Info);
static void VGARequestBIOS80x25TextMode(void);

/***************************************************************************/

#define VIDEO_CALL 0x10

/***************************************************************************/

static void VGARequestBIOS80x25TextMode(void) {
#if defined(__EXOS_ARCH_X86_32__)
    INTEL_X86_REGISTERS Registers;

    MemorySet(&Registers, 0, sizeof(Registers));
    Registers.H.AH = 0x00;
    Registers.H.AL = 0x03;
    RealModeCall(VIDEO_CALL, &Registers);
#endif
}

/***************************************************************************/

static DRIVER DATA_SECTION VGADriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = VGA_VER_MAJOR,
    .VersionMinor = VGA_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "IBM",
    .Product = "VGA Text Adapter",
    .Alias = "vga",
    .Flags = 0,
    .Command = VGACommands
};

typedef struct tag_VGA_STATE {
    GRAPHICS_CONTEXT Context;
    U16* TextBuffer;
    U32 CursorStart;
    U32 CursorEnd;
    BOOL CursorVisible;
} VGA_STATE, *LPVGA_STATE;

static VGA_STATE DATA_SECTION VGAState = {
    .Context = {.TypeID = KOID_GRAPHICSCONTEXT, .References = 1, .Mutex = EMPTY_MUTEX, .Driver = &VGADriver, .Flags = GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY},
    .TextBuffer = (U16*)(UINT)VGA_TEXT_MEMORY,
    .CursorStart = 0,
    .CursorEnd = 15,
    .CursorVisible = TRUE
};

/***************************************************************************/

/**
 * @brief Retrieve VGA driver descriptor.
 * @return Pointer to VGA driver descriptor.
 */
LPDRIVER VGAGetDriver(void) {
    return &VGADriver;
}

/***************************************************************************/

/**
 * @brief Busy-wait IO delay used between VGA port writes.
 */
void VGAIODelay(void) {
    U32 Index, Data;
    for (Index = 0; Index < 10; Index++) Data = Index;
    UNUSED(Data);
}

/***************************************************************************/

/**
 * @brief Reset the attribute controller flip-flop.
 */
static void VGAResetAttributeFlipFlop(void) {
    InPortByte(VGA_STAT);
    VGAIODelay();
}

/***************************************************************************/

/**
 * @brief Program VGA registers for a display mode.
 *
 * @param Regs Pointer to register array describing the mode.
 * @return 0 on completion.
 */
static U32 VGASendModeRegisters(U8* Regs) {
    U32 Index;

    OutPortByte(VGA_MISC, Regs[REGOFS_MISC]);

    //-------------------------------------
    // Send SEQ regs

    for (Index = 0; Index < 5; Index++) {
        OutPortByte(VGA_SEQ, Index);
        VGAIODelay();
        OutPortByte(VGA_SEQ + 1, Regs[REGOFS_SEQ + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Clear protection bits

    OutPortWord(VGA_CRTC, (((0x0E & 0x7F) << 8) | 0x11));
    VGAIODelay();

    //-------------------------------------
    // Send CRTC regs

    for (Index = 0; Index < 25; Index++) {
        OutPortByte(VGA_CRTC, Index);
        VGAIODelay();
        OutPortByte(VGA_CRTC + 1, Regs[REGOFS_CRTC + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Send GFX regs

    for (Index = 0; Index < 9; Index++) {
        OutPortByte(VGA_GFX, Index);
        VGAIODelay();
        OutPortByte(VGA_GFX + 1, Regs[REGOFS_GFX + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Send ATTR regs

    for (Index = 0; Index < 20; Index++) {
        VGAResetAttributeFlipFlop();
        OutPortByte(VGA_ATTR, Index);
        VGAIODelay();
        OutPortByte(VGA_ATTR, Regs[REGOFS_ATTR + Index]);
        VGAIODelay();
    }

    VGAResetAttributeFlipFlop();
    OutPortByte(VGA_ATTR, 0x20);

    return 0;
}

/***************************************************************************/

/**
 * @brief Compute text mode metadata from VGA registers.
 * @param Regs Pointer to register array describing the mode.
 * @param Info Output metadata structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
static BOOL VGAComputeTextModeInfo(U8* Regs, LPVGAMODEINFO Info) {
    U32 Overflow;
    U32 VerticalDisplayEnd;
    U32 CharHeight;

    if (Regs == NULL || Info == NULL) return FALSE;

    Info->Columns = (U32)Regs[REGOFS_CRTC + 1] + 1;

    Overflow = (U32)Regs[REGOFS_CRTC + 7];
    VerticalDisplayEnd = (U32)Regs[REGOFS_CRTC + 0x12];
    VerticalDisplayEnd |= (Overflow & 0x02) << 7;
    VerticalDisplayEnd |= (Overflow & 0x40) << 3;

    CharHeight = (U32)(Regs[REGOFS_CRTC + 0x09] & 0x1F) + 1;
    if (CharHeight == 0) return FALSE;

    Info->CharHeight = CharHeight;
    Info->Rows = (VerticalDisplayEnd + 1) / CharHeight;

    return Info->Columns > 0 && Info->Rows > 0;
}

/***************************************************************************/

/**
 * @brief Read one VGA CRTC register value.
 * @param RegisterIndex CRTC register index.
 * @return Register value.
 */
static U8 VGAReadCRTCRegister(U8 RegisterIndex) {
    OutPortByte(VGA_CRTC, RegisterIndex);
    VGAIODelay();
    return InPortByte(VGA_CRTC + 1);
}

/***************************************************************************/

/**
 * @brief Write one VGA CRTC register value.
 * @param RegisterIndex CRTC register index.
 * @param Value Register value.
 */
static void VGAWriteCRTCRegister(U8 RegisterIndex, U8 Value) {
    OutPortByte(VGA_CRTC, RegisterIndex);
    VGAIODelay();
    OutPortByte(VGA_CRTC + 1, Value);
    VGAIODelay();
}

/***************************************************************************/

/**
 * @brief Read active VGA text mode metadata from hardware CRTC registers.
 * @param Info Output metadata structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
static BOOL VGAReadCurrentTextModeInfo(LPVGAMODEINFO Info) {
    U32 Overflow;
    U32 VerticalDisplayEnd;
    U32 CharHeight;

    if (Info == NULL) {
        return FALSE;
    }

    Info->Columns = (U32)VGAReadCRTCRegister(0x01) + 1;

    Overflow = (U32)VGAReadCRTCRegister(0x07);
    VerticalDisplayEnd = (U32)VGAReadCRTCRegister(0x12);
    VerticalDisplayEnd |= (Overflow & 0x02) << 7;
    VerticalDisplayEnd |= (Overflow & 0x40) << 3;

    CharHeight = (U32)(VGAReadCRTCRegister(0x09) & 0x1F) + 1;
    if (CharHeight == 0) {
        return FALSE;
    }

    Info->CharHeight = CharHeight;
    Info->Rows = (VerticalDisplayEnd + 1) / CharHeight;

    return Info->Columns > 0 && Info->Rows > 0;
}

/***************************************************************************/

/**
 * @brief Refresh VGA graphics context metadata from active hardware mode.
 * @return TRUE when context is valid.
 */
static BOOL VGARefreshTextContext(void) {
    VGA_MODE_INFO ModeInfo;
    U8 CursorStart;
    U8 CursorEnd;

    if (VGAReadCurrentTextModeInfo(&ModeInfo) == FALSE) {
        return FALSE;
    }

    VGAState.Context.Width = (I32)ModeInfo.Columns;
    VGAState.Context.Height = (I32)ModeInfo.Rows;
    VGAState.Context.BitsPerPixel = 0;
    VGAState.Context.BytesPerScanLine = ModeInfo.Columns * sizeof(U16);
    VGAState.Context.MemoryBase = NULL;
    VGAState.Context.Origin.X = 0;
    VGAState.Context.Origin.Y = 0;
    VGAState.Context.LoClip.X = 0;
    VGAState.Context.LoClip.Y = 0;
    VGAState.Context.HiClip.X = (I32)ModeInfo.Columns - 1;
    VGAState.Context.HiClip.Y = (I32)ModeInfo.Rows - 1;
    VGAState.TextBuffer = (U16*)(UINT)VGA_TEXT_MEMORY;

    CursorStart = VGAReadCRTCRegister(0x0A);
    CursorEnd = VGAReadCRTCRegister(0x0B);
    VGAState.CursorStart = (U32)(CursorStart & 0x1F);
    VGAState.CursorEnd = (U32)(CursorEnd & 0x1F);
    VGAState.CursorVisible = (CursorStart & 0x20) == 0 ? TRUE : FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compose one VGA text cell value.
 * @param Character Character code.
 * @param ForegroundColorIndex Foreground palette index.
 * @param BackgroundColorIndex Background palette index.
 * @return Packed VGA text cell.
 */
static U16 VGAComposeTextCell(STR Character, U32 ForegroundColorIndex, U32 BackgroundColorIndex) {
    U16 Attribute = (U16)((ForegroundColorIndex & 0x0F) | ((BackgroundColorIndex & 0x0F) << 0x04));
    return (U16)Character | (U16)(Attribute << 0x08);
}

/***************************************************************************/

/**
 * @brief Write one text cell through the VGA backend.
 * @param Info Text cell descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VGATextPutCell(LPGFX_TEXT_CELL_INFO Info) {
    U32 Offset;

    if (Info == NULL || Info->GC != (HANDLE)&VGAState.Context) {
        return 0;
    }

    if (VGARefreshTextContext() == FALSE) {
        return 0;
    }

    if (Info->CellX >= (U32)VGAState.Context.Width || Info->CellY >= (U32)VGAState.Context.Height) {
        return 0;
    }

    Offset = (Info->CellY * (U32)VGAState.Context.Width) + Info->CellX;
    VGAState.TextBuffer[Offset] = VGAComposeTextCell(Info->Character, Info->ForegroundColorIndex, Info->BackgroundColorIndex);
    return 1;
}

/***************************************************************************/

/**
 * @brief Clear one text region through the VGA backend.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VGATextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
    U32 Row;
    U32 Column;

    if (Info == NULL || Info->GC != (HANDLE)&VGAState.Context) {
        return 0;
    }

    if (VGARefreshTextContext() == FALSE) {
        return 0;
    }

    if ((Info->CellX + Info->RegionCellWidth) > (U32)VGAState.Context.Width ||
        (Info->CellY + Info->RegionCellHeight) > (U32)VGAState.Context.Height) {
        return 0;
    }

    for (Row = 0; Row < Info->RegionCellHeight; Row++) {
        for (Column = 0; Column < Info->RegionCellWidth; Column++) {
            U32 Offset = ((Info->CellY + Row) * (U32)VGAState.Context.Width) + Info->CellX + Column;
            VGAState.TextBuffer[Offset] = VGAComposeTextCell(STR_SPACE, Info->ForegroundColorIndex, Info->BackgroundColorIndex);
        }
    }

    return 1;
}

/***************************************************************************/

/**
 * @brief Scroll one text region through the VGA backend.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VGATextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
    U32 Row;
    U32 Column;

    if (Info == NULL || Info->GC != (HANDLE)&VGAState.Context) {
        return 0;
    }

    if (VGARefreshTextContext() == FALSE) {
        return 0;
    }

    if ((Info->CellX + Info->RegionCellWidth) > (U32)VGAState.Context.Width ||
        (Info->CellY + Info->RegionCellHeight) > (U32)VGAState.Context.Height) {
        return 0;
    }

    if (Info->RegionCellHeight > 1) {
        for (Row = 1; Row < Info->RegionCellHeight; Row++) {
            for (Column = 0; Column < Info->RegionCellWidth; Column++) {
                U32 SourceOffset = ((Info->CellY + Row) * (U32)VGAState.Context.Width) + Info->CellX + Column;
                U32 DestinationOffset = ((Info->CellY + Row - 1) * (U32)VGAState.Context.Width) + Info->CellX + Column;
                VGAState.TextBuffer[DestinationOffset] = VGAState.TextBuffer[SourceOffset];
            }
        }
    }

    for (Column = 0; Column < Info->RegionCellWidth; Column++) {
        U32 Offset = ((Info->CellY + Info->RegionCellHeight - 1) * (U32)VGAState.Context.Width) + Info->CellX + Column;
        VGAState.TextBuffer[Offset] = VGAComposeTextCell(STR_SPACE, Info->ForegroundColorIndex, Info->BackgroundColorIndex);
    }

    return 1;
}

/***************************************************************************/

/**
 * @brief Program VGA hardware cursor position.
 * @param Info Cursor descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VGATextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
    U32 Position;

    if (Info == NULL || Info->GC != (HANDLE)&VGAState.Context) {
        return 0;
    }

    if (VGARefreshTextContext() == FALSE) {
        return 0;
    }

    if (Info->CellX >= (U32)VGAState.Context.Width || Info->CellY >= (U32)VGAState.Context.Height) {
        return 0;
    }

    Position = (Info->CellY * (U32)VGAState.Context.Width) + Info->CellX;
    VGAWriteCRTCRegister(0x0E, (U8)((Position >> 8) & 0xFF));
    VGAWriteCRTCRegister(0x0F, (U8)(Position & 0xFF));
    return 1;
}

/***************************************************************************/

/**
 * @brief Program VGA hardware cursor visibility.
 * @param Info Cursor visibility descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VGATextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    U8 Start;
    U8 End;

    if (Info == NULL || Info->GC != (HANDLE)&VGAState.Context) {
        return 0;
    }

    if (VGARefreshTextContext() == FALSE) {
        return 0;
    }

    Start = (U8)(VGAState.CursorStart & 0x1F);
    End = (U8)(VGAState.CursorEnd & 0x1F);
    if (Info->IsVisible == FALSE) {
        Start |= 0x20;
    }

    VGAWriteCRTCRegister(0x0A, Start);
    VGAWriteCRTCRegister(0x0B, End);
    VGAState.CursorVisible = Info->IsVisible ? TRUE : FALSE;
    return 1;
}

/***************************************************************************/

/**
 * @brief Set VGA text mode from a generic graphics mode request.
 * @param Info Input/output mode descriptor.
 * @return DF_RETURN_SUCCESS on success or DF_GFX_ERROR_MODEUNAVAIL.
 */
static UINT VGASetModeFromRequest(LPGRAPHICS_MODE_INFO Info) {
    U32 RequestedColumns = 0;
    U32 RequestedRows = 0;
    U32 ModeIndex = 0;
    VGA_MODE_INFO ModeInfo;

    SAFE_USE(Info) {
        RequestedColumns = (Info->Width != 0) ? Info->Width : 80;
        RequestedRows = (Info->Height != 0) ? Info->Height : 25;

        if (VGAFindTextMode(RequestedColumns, RequestedRows, &ModeIndex) == FALSE) {
            return DF_GFX_ERROR_MODEUNAVAIL;
        }

        if (RequestedColumns == 80 && RequestedRows == 25) {
            VGARequestBIOS80x25TextMode();
        }

        if (VGASetMode(ModeIndex) == FALSE) {
            return DF_GFX_ERROR_MODEUNAVAIL;
        }

        if (VGAGetModeInfo(ModeIndex, &ModeInfo) == FALSE) {
            ModeInfo.Columns = RequestedColumns;
            ModeInfo.Rows = RequestedRows;
        }

        (void)VGARefreshTextContext();

        Info->Width = ModeInfo.Columns;
        Info->Height = ModeInfo.Rows;
        Info->BitsPerPixel = 0;

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief VGA driver command dispatcher.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver return code.
 */
static UINT VGACommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            VGADriver.Flags |= DRIVER_FLAG_READY;
            (void)VGARefreshTextContext();
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            VGADriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(VGA_VER_MAJOR, VGA_VER_MINOR);

        case DF_GFX_GETMODECOUNT:
            return VGAGetModeCount();

        case DF_GFX_GETMODEINFO: {
            LPGRAPHICS_MODE_INFO Info = (LPGRAPHICS_MODE_INFO)Parameter;
            VGA_MODE_INFO ModeInfo;

            SAFE_USE(Info) {
                if (Info->ModeIndex == INFINITY) {
                    if (VGAReadCurrentTextModeInfo(&ModeInfo) == FALSE) {
                        if (VGAGetModeInfo(0, &ModeInfo) == FALSE) {
                            ModeInfo.Columns = 80;
                            ModeInfo.Rows = 25;
                        }
                    }
                } else {
                    if (VGAGetModeInfo(Info->ModeIndex, &ModeInfo) == FALSE) {
                        return DF_GFX_ERROR_MODEUNAVAIL;
                    }
                }

                Info->Width = ModeInfo.Columns;
                Info->Height = ModeInfo.Rows;
                Info->BitsPerPixel = 0;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_BAD_PARAMETER;
        }

        case DF_GFX_SETMODE:
            return VGASetModeFromRequest((LPGRAPHICS_MODE_INFO)Parameter);

        case DF_GFX_GETCONTEXT:
            if ((VGADriver.Flags & DRIVER_FLAG_READY) == 0) {
                return 0;
            }
            return VGARefreshTextContext() != FALSE ? (UINT)(LPVOID)&VGAState.Context : 0;

        case DF_GFX_TEXT_PUTCELL:
            return VGATextPutCell((LPGFX_TEXT_CELL_INFO)Parameter);

        case DF_GFX_TEXT_CLEAR_REGION:
            return VGATextClearRegion((LPGFX_TEXT_REGION_INFO)Parameter);

        case DF_GFX_TEXT_SCROLL_REGION:
            return VGATextScrollRegion((LPGFX_TEXT_REGION_INFO)Parameter);

        case DF_GFX_TEXT_SET_CURSOR:
            return VGATextSetCursor((LPGFX_TEXT_CURSOR_INFO)Parameter);

        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
            return VGATextSetCursorVisible((LPGFX_TEXT_CURSOR_VISIBLE_INFO)Parameter);

        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
        case DF_GFX_ELLIPSE:
        case DF_GFX_GETCAPABILITIES:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_PRESENT:
        case DF_GFX_WAITVBLANK:
        case DF_GFX_ALLOCSURFACE:
        case DF_GFX_FREESURFACE:
        case DF_GFX_SETSCANOUT:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/***************************************************************************/

/**
 * @brief Return the number of VGA modes in the table.
 * @return Count of VGA modes.
 */
U32 VGAGetModeCount(void) { return VGAModeRegsCount; }

/***************************************************************************/

/**
 * @brief Retrieve text mode information for a VGA mode index.
 * @param ModeIndex Index in the VGA mode table.
 * @param Info Output mode information.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL VGAGetModeInfo(U32 ModeIndex, LPVGAMODEINFO Info) {
    if (ModeIndex >= VGAModeRegsCount) return FALSE;
    return VGAComputeTextModeInfo(VGAModeRegs[ModeIndex].Regs, Info);
}

/***************************************************************************/

/**
 * @brief Find a VGA text mode by columns and rows.
 * @param Columns Desired text columns.
 * @param Rows Desired text rows.
 * @param ModeIndex Output index on success.
 * @return TRUE if a matching mode was found.
 */
BOOL VGAFindTextMode(U32 Columns, U32 Rows, U32* ModeIndex) {
    U32 Index;
    VGA_MODE_INFO Info;

    if (ModeIndex == NULL) return FALSE;

    for (Index = 0; Index < VGAModeRegsCount; Index++) {
        if (VGAGetModeInfo(Index, &Info) == FALSE) continue;
        if (Info.Columns == Columns && Info.Rows == Rows) {
            *ModeIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Program VGA registers for a mode index.
 * @param ModeIndex Index in the VGA mode table.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL VGASetMode(U32 ModeIndex) {
    if (ModeIndex >= VGAModeRegsCount) return FALSE;
    return VGASendModeRegisters(VGAModeRegs[ModeIndex].Regs) == 0;
}

/***************************************************************************/
