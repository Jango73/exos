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


    Intel graphics (mode takeover and native setmode)

\************************************************************************/

#include "iGPU-Internal.h"

#include "system/Clock.h"
#include "text/CoreString.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "utils/Graphics-Utils.h"

/************************************************************************/

#define INTEL_MODESET_STAGE_DISABLE_PIPE (1 << 0)
#define INTEL_MODESET_STAGE_ROUTE_TRANSCODER (1 << 1)
#define INTEL_MODESET_STAGE_PROGRAM_CLOCK (1 << 2)
#define INTEL_MODESET_STAGE_CONFIGURE_LINK (1 << 3)
#define INTEL_MODESET_STAGE_SCANOUT_MEMORY (1 << 4)
#define INTEL_MODESET_STAGE_DISABLE_COMPRESSION (1 << 5)
#define INTEL_MODESET_STAGE_ENABLE_PIPE (1 << 6)
#define INTEL_MODESET_STAGE_PANEL_STABILITY (1 << 7)
#define INTEL_MODESET_HBLANK_EXTRA 160
#define INTEL_MODESET_HSYNC_START_OFFSET 48
#define INTEL_MODESET_HSYNC_PULSE_WIDTH 32
#define INTEL_MODESET_VBLANK_EXTRA 30
#define INTEL_MODESET_VSYNC_START_OFFSET 3
#define INTEL_MODESET_VSYNC_PULSE_WIDTH 5

/************************************************************************/

static const U32 IntelPipeConfRegisters[] = {INTEL_REG_PIPE_A_CONF, INTEL_REG_PIPE_B_CONF, INTEL_REG_PIPE_C_CONF};
static const U32 IntelPipeSourceRegisters[] = {INTEL_REG_PIPE_A_SRC, INTEL_REG_PIPE_B_SRC, INTEL_REG_PIPE_C_SRC};
static const U32 IntelPipeHTotalRegisters[] = {INTEL_REG_PIPE_A_HTOTAL, INTEL_REG_PIPE_B_HTOTAL, INTEL_REG_PIPE_C_HTOTAL};
static const U32 IntelPipeHBlankRegisters[] = {INTEL_REG_PIPE_A_HBLANK, INTEL_REG_PIPE_B_HBLANK, INTEL_REG_PIPE_C_HBLANK};
static const U32 IntelPipeHSyncRegisters[] = {INTEL_REG_PIPE_A_HSYNC, INTEL_REG_PIPE_B_HSYNC, INTEL_REG_PIPE_C_HSYNC};
static const U32 IntelPipeVTotalRegisters[] = {INTEL_REG_PIPE_A_VTOTAL, INTEL_REG_PIPE_B_VTOTAL, INTEL_REG_PIPE_C_VTOTAL};
static const U32 IntelPipeVBlankRegisters[] = {INTEL_REG_PIPE_A_VBLANK, INTEL_REG_PIPE_B_VBLANK, INTEL_REG_PIPE_C_VBLANK};
static const U32 IntelPipeVSyncRegisters[] = {INTEL_REG_PIPE_A_VSYNC, INTEL_REG_PIPE_B_VSYNC, INTEL_REG_PIPE_C_VSYNC};
static const U32 IntelPlaneControlRegisters[] = {INTEL_REG_PLANE_A_CTL, INTEL_REG_PLANE_B_CTL, INTEL_REG_PLANE_C_CTL};
static const U32 IntelPlaneStrideRegisters[] = {INTEL_REG_PLANE_A_STRIDE, INTEL_REG_PLANE_B_STRIDE, INTEL_REG_PLANE_C_STRIDE};
static const U32 IntelPlaneSurfaceRegisters[] = {INTEL_REG_PLANE_A_SURF, INTEL_REG_PLANE_B_SURF, INTEL_REG_PLANE_C_SURF};
static const U32 IntelDdiBufferControlRegisters[] = {
    INTEL_REG_DDI_BUF_CTL_A,
    INTEL_REG_DDI_BUF_CTL_B,
    INTEL_REG_DDI_BUF_CTL_C,
    INTEL_REG_DDI_BUF_CTL_D,
    INTEL_REG_DDI_BUF_CTL_E
};
static const U32 IntelTranscoderDdiRegisters[] = {INTEL_REG_TRANS_DDI_FUNC_CTL_A, INTEL_REG_TRANS_DDI_FUNC_CTL_B, INTEL_REG_TRANS_DDI_FUNC_CTL_C};
static const U32 IntelPortMaskByIndex[] = {INTEL_PORT_A, INTEL_PORT_B, INTEL_PORT_C, INTEL_PORT_D, INTEL_PORT_E};

/************************************************************************/

static U32 IntelGfxResolveBitsPerPixel(U32 PlaneControlValue) {
    U32 Format = PlaneControlValue & INTEL_PLANE_CTL_FORMAT_MASK;

    switch (Format) {
        case (0x02 << 24):
            return 16;
        case (0x04 << 24):
            return 32;
        case (0x06 << 24):
            return 32;
        default:
            return 32;
    }
}

/************************************************************************/

static BOOL IntelGfxReadRegister32Safe(U32 RegisterOffset, U32* ValueOut) {
    if (ValueOut == NULL || RegisterOffset == 0) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(RegisterOffset, ValueOut)) {
        return FALSE;
    }

    return (*ValueOut != 0xFFFFFFFF) ? TRUE : FALSE;
}

/************************************************************************/

static BOOL IntelGfxWriteVerifyRegister32(U32 RegisterOffset, U32 Value, U32 Mask) {
    U32 ReadBack = 0;

    if (RegisterOffset == 0) {
        return TRUE;
    }

    if (!IntelGfxWriteMmio32(RegisterOffset, Value)) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(RegisterOffset, &ReadBack)) {
        ERROR(TEXT("Readback failed reg=%x"), RegisterOffset);
        return FALSE;
    }

    if ((ReadBack & Mask) != (Value & Mask)) {
        ERROR(TEXT("Verify failed reg=%x write=%x read=%x mask=%x"),
            RegisterOffset,
            Value,
            ReadBack,
            Mask);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static U32 IntelGfxAlignUp(U32 Value, U32 Alignment) {
    U32 Mask = 0;

    if (Alignment <= 1) {
        return Value;
    }

    Mask = Alignment - 1;
    return (Value + Mask) & ~Mask;
}

/************************************************************************/

static void IntelGfxPrimeScanoutPattern(LINEAR FrameBuffer, U32 Width, U32 Height, U32 Stride) {
    U32 Y = 0;
    U32 X = 0;

    if (FrameBuffer == 0 || Width == 0 || Height == 0 || Stride < Width * 4) {
        return;
    }

    for (Y = 0; Y < Height; Y++) {
        U32* Row = (U32*)(LINEAR)(FrameBuffer + ((LINEAR)Y * Stride));
        for (X = 0; X < Width; X++) {
            U32 Red = ((X >> 3) & 0x1F) << 16;
            U32 Green = ((Y >> 2) & 0x3F) << 8;
            U32 Blue = ((X + Y) >> 4) & 0x1F;
            Row[X] = 0xFF000000 | Red | Green | Blue;
        }
    }
}

/************************************************************************/

static UINT IntelGfxPrepareScanoutMemory(LPINTEL_GFX_MODE_PROGRAM Program) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 Bar2Raw = 0;
    U32 Bar2Base = 0;
    U32 Bar2Size = 0;
    U32 RequiredSize = 0;
    U32 SurfaceOffset = 0;
    U32 SurfaceAlignment = 0;
    PHYSICAL SurfacePhysical = 0;
    LINEAR SurfaceLinear = 0;

    if (Program == NULL || IntelGfxState.Device == NULL || Family == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    RequiredSize = Program->PlaneStride * Program->Height;
    if (RequiredSize == 0) {
        ERROR(TEXT("Invalid scanout size stride=%u height=%u"),
            Program->PlaneStride,
            Program->Height);
        return DF_RETURN_UNEXPECTED;
    }

    Bar2Raw = IntelGfxState.Device->Info.BAR[2];
    if (PCI_BAR_IS_IO(Bar2Raw)) {
        ERROR(TEXT("BAR2 is I/O (bar2=%x)"), Bar2Raw);
        return DF_RETURN_UNEXPECTED;
    }

    Bar2Base = PCI_GetBARBase(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    Bar2Size = PCI_GetBARSize(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    if (Bar2Base == 0 || Bar2Size == 0) {
        ERROR(TEXT("Invalid BAR2 base=%x size=%u"), Bar2Base, Bar2Size);
        return DF_RETURN_UNEXPECTED;
    }

    SurfaceAlignment = Family->SurfaceAlignment ? Family->SurfaceAlignment : 0x1000;

    if (IntelGfxState.HasActiveMode != FALSE && IntelGfxState.ActiveSurfaceOffset < Bar2Size) {
        SurfaceOffset = IntelGfxState.ActiveSurfaceOffset;
    } else {
        SurfaceOffset = Family->ColdSurfaceOffset;
    }

    SurfaceOffset = IntelGfxAlignUp(SurfaceOffset, SurfaceAlignment);
    if (SurfaceOffset >= Bar2Size || RequiredSize > (Bar2Size - SurfaceOffset)) {
        ERROR(TEXT("Surface window invalid offset=%x size=%u required=%u"),
            SurfaceOffset,
            Bar2Size,
            RequiredSize);
        return DF_RETURN_UNEXPECTED;
    }

    Program->PlaneSurface = SurfaceOffset & INTEL_SURFACE_ALIGN_MASK;
    SurfacePhysical = (PHYSICAL)(Bar2Base + Program->PlaneSurface);
    SurfaceLinear = MapIOMemory(SurfacePhysical, RequiredSize);
    if (SurfaceLinear == 0) {
        ERROR(TEXT("MapIOMemory failed base=%p size=%u"),
            (LPVOID)(LINEAR)SurfacePhysical,
            RequiredSize);
        return DF_RETURN_UNEXPECTED;
    }

    MemorySet((LPVOID)(LINEAR)SurfaceLinear, 0, RequiredSize);
#if DEBUG_OUTPUT == 1
    IntelGfxPrimeScanoutPattern(SurfaceLinear, Program->Width, Program->Height, Program->PlaneStride);
#endif
    UnMapIOMemory(SurfaceLinear, RequiredSize);

    DEBUG(TEXT("Scanout prepared offset=%x size=%u"), Program->PlaneSurface, RequiredSize);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxDisableCompressionBlocks(LPINTEL_GFX_MODE_PROGRAM Program) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 CurrentValue = 0;
    U32 NewValue = 0;

    if (Program == NULL || Family == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->CompressionControlRegister = 0;
    Program->CompressionControlValue = 0;

    if (Family->RequireCompressionDisable == FALSE) {
        return DF_RETURN_SUCCESS;
    }

    if (Family->CompressionControlRegister == 0 || Family->CompressionControlEnableMask == 0) {
        WARNING(TEXT("Compression disable metadata missing for family=%s"), Family->Name);
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxReadRegister32Safe(Family->CompressionControlRegister, &CurrentValue)) {
        WARNING(TEXT("Compression register unavailable reg=%x"),
            Family->CompressionControlRegister);
        return DF_RETURN_SUCCESS;
    }

    NewValue = CurrentValue & ~Family->CompressionControlEnableMask;
    if (!IntelGfxWriteVerifyRegister32(
            Family->CompressionControlRegister,
            NewValue,
            Family->CompressionControlEnableMask)) {
        ERROR(TEXT("Compression disable failed reg=%x"),
            Family->CompressionControlRegister);
        return DF_RETURN_UNEXPECTED;
    }

    Program->CompressionControlRegister = Family->CompressionControlRegister;
    Program->CompressionControlValue = NewValue;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static U32 IntelGfxPortMaskFromIndex(U32 PortIndex) {
    if (PortIndex >= sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0])) {
        return 0;
    }

    return IntelPortMaskByIndex[PortIndex];
}

/************************************************************************/

static BOOL IntelGfxPortIndexFromMask(U32 PortMask, U32* PortIndexOut) {
    U32 Index = 0;

    if (PortIndexOut == NULL || PortMask == 0) {
        return FALSE;
    }

    for (Index = 0; Index < sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0]); Index++) {
        if (IntelPortMaskByIndex[Index] == PortMask) {
            *PortIndexOut = Index;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static U32 IntelGfxFindFirstPortFromMask(U32 PortMask) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0]); Index++) {
        if ((PortMask & IntelPortMaskByIndex[Index]) != 0) {
            return IntelPortMaskByIndex[Index];
        }
    }

    return 0;
}

/************************************************************************/

static U32 IntelGfxResolveOutputTypeFromPort(U32 PortMask) {
    if (PortMask == INTEL_PORT_A) {
        return GFX_OUTPUT_TYPE_EDP;
    }

    if (PortMask == INTEL_PORT_B || PortMask == INTEL_PORT_C || PortMask == INTEL_PORT_D) {
        return GFX_OUTPUT_TYPE_DISPLAYPORT;
    }

    if (PortMask == INTEL_PORT_E) {
        return GFX_OUTPUT_TYPE_HDMI;
    }

    return GFX_OUTPUT_TYPE_UNKNOWN;
}

/************************************************************************/

static U32 IntelGfxFindActiveOutputPortMask(void) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0]); Index++) {
        U32 Value = 0;

        if (!IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[Index], &Value)) {
            continue;
        }

        if ((Value & INTEL_DDI_BUF_CTL_ENABLE) == 0) {
            continue;
        }

        return IntelGfxPortMaskFromIndex(Index);
    }

    return 0;
}

/************************************************************************/

static BOOL IntelGfxReadActiveScanoutState(void) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0]); Index++) {
        U32 PipeConf = 0;
        U32 PipeSrc = 0;
        U32 PlaneControl = 0;
        U32 PlaneStride = 0;
        U32 PlaneSurface = 0;
        U32 Width = 0;
        U32 Height = 0;
        U32 BitsPerPixel = 0;
        U32 Stride = 0;
        U32 ActivePortMask = 0;

        if (!IntelGfxReadMmio32(IntelPipeConfRegisters[Index], &PipeConf)) continue;
        if ((PipeConf & INTEL_PIPE_CONF_ENABLE) == 0) continue;

        if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[Index], &PlaneControl)) continue;
        if ((PlaneControl & INTEL_PLANE_CTL_ENABLE) == 0) continue;

        if (!IntelGfxReadMmio32(IntelPipeSourceRegisters[Index], &PipeSrc)) continue;
        if (!IntelGfxReadMmio32(IntelPlaneStrideRegisters[Index], &PlaneStride)) continue;
        if (!IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[Index], &PlaneSurface)) continue;

        Width = (PipeSrc & 0x1FFF) + 1;
        Height = ((PipeSrc >> 16) & 0x1FFF) + 1;
        BitsPerPixel = IntelGfxResolveBitsPerPixel(PlaneControl);
        Stride = IntelGfxResolveStrideFromReadback(PlaneStride, Width, BitsPerPixel);

        ActivePortMask = IntelGfxFindActiveOutputPortMask();
        if (ActivePortMask == 0) {
            ActivePortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
        }

        IntelGfxState.ActivePipeIndex = Index;
        IntelGfxState.ActiveWidth = Width;
        IntelGfxState.ActiveHeight = Height;
        IntelGfxState.ActiveBitsPerPixel = BitsPerPixel;
        IntelGfxState.ActiveStride = Stride;
        IntelGfxState.ActiveSurfaceOffset = PlaneSurface & INTEL_SURFACE_ALIGN_MASK;
        IntelGfxState.ActiveOutputPortMask = ActivePortMask;
        IntelGfxState.ActiveTranscoderIndex = Index;
        IntelGfxState.HasActiveMode = TRUE;

        DEBUG(TEXT("Pipe=%u Width=%u Height=%u Bpp=%u Stride=%u Surface=%x Port=%x"),
            Index,
            Width,
            Height,
            BitsPerPixel,
            Stride,
            IntelGfxState.ActiveSurfaceOffset,
            ActivePortMask);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL IntelGfxReadModeProgram(U32 PipeIndex, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    U32 OutputPortMask = 0;

    if (ProgramOut == NULL || PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    ProgramOut->PipeIndex = PipeIndex;
    ProgramOut->Width = IntelGfxState.ActiveWidth;
    ProgramOut->Height = IntelGfxState.ActiveHeight;
    ProgramOut->BitsPerPixel = IntelGfxState.ActiveBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;

    if (!(IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &ProgramOut->PipeConf) &&
            IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &ProgramOut->PipeSource) &&
            IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &ProgramOut->PipeHTotal) &&
            IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &ProgramOut->PipeHBlank) &&
            IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &ProgramOut->PipeHSync) &&
            IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &ProgramOut->PipeVTotal) &&
            IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &ProgramOut->PipeVBlank) &&
            IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &ProgramOut->PipeVSync) &&
            IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &ProgramOut->PlaneControl) &&
            IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &ProgramOut->PlaneStride) &&
            IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &ProgramOut->PlaneSurface))) {
        return FALSE;
    }

    OutputPortMask = IntelGfxFindActiveOutputPortMask();
    if (OutputPortMask == 0) {
        OutputPortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
    }

    ProgramOut->OutputPortMask = OutputPortMask;
    ProgramOut->OutputType = IntelGfxResolveOutputTypeFromPort(OutputPortMask);
    ProgramOut->TranscoderIndex = (PipeIndex < IntelGfxState.IntelCapabilities.TranscoderCount) ? PipeIndex : 0;

    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxWaitPipeState(U32 PipeIndex, BOOL EnabledExpected) {
    UINT StartTime = GetSystemTime();
    UINT Loop = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    for (Loop = 0; HasOperationTimedOut(StartTime, Loop, INTEL_MODESET_LOOP_LIMIT, INTEL_MODESET_TIMEOUT_MILLISECONDS) == FALSE; Loop++) {
        U32 PipeConf = 0;
        BOOL Enabled = FALSE;

        if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
            return FALSE;
        }

        Enabled = (PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
        if (Enabled == EnabledExpected) {
            return TRUE;
        }
    }

    if (PipeIndex < sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        U32 PipeConf = 0;
        if (IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
            WARNING(TEXT("Timeout pipe=%u expected=%u conf=%x loops=%u"),
                PipeIndex,
                EnabledExpected ? 1 : 0,
                PipeConf,
                Loop);
        }
    }

    return FALSE;
}

/************************************************************************/

static void IntelGfxDumpPipeRegisters(U32 PipeIndex, LPCSTR PrefixTag) {
    U32 PipeConf = 0;
    U32 PlaneControl = 0;
    U32 PipeSource = 0;
    U32 PipeHTotal = 0;
    U32 PipeHBlank = 0;
    U32 PipeHSync = 0;
    U32 PipeVTotal = 0;
    U32 PipeVBlank = 0;
    U32 PipeVSync = 0;
    U32 PlaneStride = 0;
    U32 PlaneSurface = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return;
    }

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf) ||
        !IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl) ||
        !IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &PipeSource) ||
        !IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &PipeHTotal) ||
        !IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &PipeHBlank) ||
        !IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &PipeHSync) ||
        !IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &PipeVTotal) ||
        !IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &PipeVBlank) ||
        !IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &PipeVSync) ||
        !IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &PlaneStride) ||
        !IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &PlaneSurface)) {
        WARNING(TEXT("%s pipe=%u dump failed"), PrefixTag, PipeIndex);
        return;
    }

    WARNING(TEXT("%s pipe=%u conf=%x ctl=%x src=%x stride=%x surf=%x"),
        PrefixTag,
        PipeIndex,
        PipeConf,
        PlaneControl,
        PipeSource,
        PlaneStride,
        PlaneSurface);
    WARNING(TEXT("%s pipe=%u htotal=%x hblank=%x hsync=%x vtotal=%x vblank=%x vsync=%x"),
        PrefixTag,
        PipeIndex,
        PipeHTotal,
        PipeHBlank,
        PipeHSync,
        PipeVTotal,
        PipeVBlank,
        PipeVSync);
}

/************************************************************************/

static BOOL IntelGfxVerifyPipeEnabledState(U32 PipeIndex, BOOL ExpectedPipeEnabled, BOOL ExpectedPlaneEnabled) {
    U32 PipeConf = 0;
    U32 PlaneControl = 0;
    BOOL PipeEnabled = FALSE;
    BOOL PlaneEnabled = FALSE;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl)) {
        return FALSE;
    }

    PipeEnabled = (PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
    PlaneEnabled = (PlaneControl & INTEL_PLANE_CTL_ENABLE) ? TRUE : FALSE;

    return (PipeEnabled == ExpectedPipeEnabled && PlaneEnabled == ExpectedPlaneEnabled) ? TRUE : FALSE;
}

/************************************************************************/

static UINT IntelGfxDisablePipe(U32 PipeIndex) {
    U32 PlaneControl = 0;
    U32 PipeConf = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl)) {
        return DF_RETURN_UNEXPECTED;
    }

    PlaneControl &= ~INTEL_PLANE_CTL_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPlaneControlRegisters[PipeIndex], PlaneControl)) {
        return DF_RETURN_UNEXPECTED;
    }
    (void)IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl);

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeConf &= ~INTEL_PIPE_CONF_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPipeConfRegisters[PipeIndex], PipeConf)) {
        return DF_RETURN_UNEXPECTED;
    }
    (void)IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf);

    if (!IntelGfxWaitPipeState(PipeIndex, FALSE)) {
        ERROR(TEXT("Pipe=%u disable timeout"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxVerifyPipeEnabledState(PipeIndex, FALSE, FALSE)) {
        ERROR(TEXT("Pipe=%u disable verification failed"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramTranscoderRoute(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 PortIndex = 0;
    U32 TranscoderControl = 0;
    U32 RegisterOffset = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (Program->TranscoderIndex >= sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0])) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxPortIndexFromMask(Program->OutputPortMask, &PortIndex)) {
        return DF_RETURN_UNEXPECTED;
    }

    RegisterOffset = IntelTranscoderDdiRegisters[Program->TranscoderIndex];
    if (!IntelGfxReadRegister32Safe(RegisterOffset, &TranscoderControl)) {
        WARNING(TEXT("Transcoder register unavailable (transcoder=%u)"), Program->TranscoderIndex);
        return DF_RETURN_SUCCESS;
    }

    TranscoderControl &= ~INTEL_TRANS_DDI_FUNC_PORT_MASK;
    TranscoderControl |= (PortIndex << INTEL_TRANS_DDI_FUNC_PORT_SHIFT);
    TranscoderControl |= INTEL_TRANS_DDI_FUNC_ENABLE;

    if (!IntelGfxWriteVerifyRegister32(RegisterOffset, TranscoderControl, INTEL_TRANS_DDI_FUNC_PORT_MASK | INTEL_TRANS_DDI_FUNC_ENABLE)) {
        ERROR(TEXT("Transcoder route write failed (transcoder=%u port=%x)"),
            Program->TranscoderIndex,
            Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    Program->TranscoderControl = TranscoderControl;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramClockSource(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 ClockControl = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->ClockControlRegister = 0;
    Program->ClockControlValue = 0;

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxReadRegister32Safe(INTEL_REG_DPLL_CTRL1, &ClockControl)) {
        WARNING(TEXT("DPLL control register unavailable"));
        return DF_RETURN_SUCCESS;
    }

    Program->ClockControlRegister = INTEL_REG_DPLL_CTRL1;
    Program->ClockControlValue = ClockControl;

    // Conservative multi-family path: preserve active DPLL source selection.
    if (!IntelGfxWriteVerifyRegister32(Program->ClockControlRegister, Program->ClockControlValue, MAX_U32)) {
        ERROR(TEXT("DPLL programming failed"));
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxConfigureConnectorLink(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 PortIndex = 0;
    U32 LinkControl = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxPortIndexFromMask(Program->OutputPortMask, &PortIndex)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (PortIndex >= sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->LinkControlRegister = IntelDdiBufferControlRegisters[PortIndex];
    if (!IntelGfxReadRegister32Safe(Program->LinkControlRegister, &LinkControl)) {
        ERROR(TEXT("DDI link register unavailable for port=%x"), Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    LinkControl |= INTEL_DDI_BUF_CTL_ENABLE;

    if (!IntelGfxWriteVerifyRegister32(Program->LinkControlRegister, LinkControl, INTEL_DDI_BUF_CTL_ENABLE)) {
        ERROR(TEXT("Link enable failed for port=%x"), Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    Program->LinkControlValue = LinkControl;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramPanelStability(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 Value = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->PanelPowerRegister = 0;
    Program->PanelPowerValue = 0;
    Program->BacklightRegister = 0;
    Program->BacklightValue = 0;

    if (Program->OutputType != GFX_OUTPUT_TYPE_EDP) {
        return DF_RETURN_SUCCESS;
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_PP_CONTROL, &Value)) {
        Program->PanelPowerRegister = INTEL_REG_PP_CONTROL;
        Program->PanelPowerValue = Value | INTEL_PANEL_POWER_TARGET_ON;

        if (!IntelGfxWriteVerifyRegister32(Program->PanelPowerRegister, Program->PanelPowerValue, INTEL_PANEL_POWER_TARGET_ON)) {
            ERROR(TEXT("Panel power target programming failed"));
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_BLC_PWM_CTL2, &Value)) {
        Program->BacklightRegister = INTEL_REG_BLC_PWM_CTL2;
        Program->BacklightValue = Value | INTEL_BACKLIGHT_PWM_ENABLE;

        if (!IntelGfxWriteVerifyRegister32(Program->BacklightRegister, Program->BacklightValue, INTEL_BACKLIGHT_PWM_ENABLE)) {
            ERROR(TEXT("Backlight programming failed"));
            return DF_RETURN_UNEXPECTED;
        }
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxEnablePipe(LPINTEL_GFX_MODE_PROGRAM Program) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 PipeConf = 0;
    U32 PlaneControl = 0;
    U32 PipeIndex = 0;
    U32 EncodedStride = 0;
    U32 ProgrammedStride = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }
    if (Family == NULL) {
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    PipeIndex = Program->PipeIndex;
    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWriteVerifyRegister32(IntelPipeHTotalRegisters[PipeIndex], Program->PipeHTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHBlankRegisters[PipeIndex], Program->PipeHBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHSyncRegisters[PipeIndex], Program->PipeHSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVTotalRegisters[PipeIndex], Program->PipeVTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVBlankRegisters[PipeIndex], Program->PipeVBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVSyncRegisters[PipeIndex], Program->PipeVSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeSourceRegisters[PipeIndex], Program->PipeSource, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneSurfaceRegisters[PipeIndex], Program->PlaneSurface, INTEL_SURFACE_ALIGN_MASK)) {
        ERROR(TEXT("Timing or plane programming failed pipe=%u"), PipeIndex);
        IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-program"));
        return DF_RETURN_UNEXPECTED;
    }

    EncodedStride = IntelGfxEncodeProgramStride(Program->PlaneStride);
    if (EncodedStride == 0) {
        ERROR(TEXT("Invalid encoded stride pipe=%u strideBytes=%u"), PipeIndex, Program->PlaneStride);
        return DF_RETURN_UNEXPECTED;
    }
    DEBUG(TEXT("Stride encode pipe=%u bytes=%u encoded=%x"), PipeIndex, Program->PlaneStride, EncodedStride);

    if (!IntelGfxWriteVerifyRegister32(IntelPlaneStrideRegisters[PipeIndex], EncodedStride, Family->StrideWriteMask)) {
        if (IntelGfxState.HasActiveMode == FALSE) {
            if (!IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &ProgrammedStride)) {
                ERROR(TEXT("Cold stride fallback read failed pipe=%u"), PipeIndex);
                IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-stride-fallback-read"));
                return DF_RETURN_UNEXPECTED;
            }

            ProgrammedStride = IntelGfxResolveStrideFromReadback(ProgrammedStride, Program->Width, Program->BitsPerPixel);
            WARNING(TEXT("Cold stride fallback pipe=%u requested=%x programmed=%x"),
                PipeIndex,
                Program->PlaneStride,
                ProgrammedStride);

            if (ProgrammedStride == 0) {
                return DF_RETURN_UNEXPECTED;
            }

            Program->PlaneStride = ProgrammedStride;
        } else {
            ERROR(TEXT("Stride programming failed pipe=%u"), PipeIndex);
            IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-stride"));
            return DF_RETURN_UNEXPECTED;
        }
    }

    PipeConf = Program->PipeConf | INTEL_PIPE_CONF_ENABLE;
    if (!IntelGfxWriteVerifyRegister32(IntelPipeConfRegisters[PipeIndex], PipeConf, INTEL_PIPE_CONF_ENABLE)) {
        ERROR(TEXT("PipeConf enable write failed pipe=%u value=%x"), PipeIndex, PipeConf);
        IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-pipeconf"));
        return DF_RETURN_UNEXPECTED;
    }

    PlaneControl = Program->PlaneControl;
    PlaneControl = IntelGfxBuildPlaneControl(PlaneControl);
    PlaneControl |= INTEL_PLANE_CTL_ENABLE;
    if (!IntelGfxWriteVerifyRegister32(
            IntelPlaneControlRegisters[PipeIndex],
            PlaneControl,
            INTEL_PLANE_CTL_ENABLE | INTEL_PLANE_CTL_FORMAT_MASK | INTEL_PLANE_CTL_TILING_MASK)) {
        ERROR(TEXT("Plane enable write failed pipe=%u value=%x"), PipeIndex, PlaneControl);
        IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-planectl"));
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWaitPipeState(PipeIndex, TRUE)) {
        ERROR(TEXT("Pipe=%u enable timeout"), PipeIndex);
        IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-timeout"));
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxVerifyPipeEnabledState(PipeIndex, TRUE, TRUE)) {
        ERROR(TEXT("Pipe=%u enable verification failed"), PipeIndex);
        IntelGfxDumpPipeRegisters(PipeIndex, TEXT("EnablePipe-verify"));
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxSelectPipeOutputRouting(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 AvailablePortMask = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    AvailablePortMask = IntelGfxState.IntelCapabilities.PortMask;

    if (IntelGfxState.ActiveOutputPortMask != 0 && (IntelGfxState.ActiveOutputPortMask & AvailablePortMask) != 0) {
        Program->OutputPortMask = IntelGfxState.ActiveOutputPortMask;
    } else if ((INTEL_PORT_A & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_A;
    } else if ((INTEL_PORT_B & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_B;
    } else if ((INTEL_PORT_C & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_C;
    } else if ((INTEL_PORT_D & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_D;
    } else if ((INTEL_PORT_E & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_E;
    } else {
        return DF_RETURN_UNEXPECTED;
    }

    Program->OutputType = IntelGfxResolveOutputTypeFromPort(Program->OutputPortMask);
    Program->TranscoderIndex = Program->PipeIndex;
    if (Program->TranscoderIndex >= IntelGfxState.IntelCapabilities.TranscoderCount) {
        Program->TranscoderIndex = IntelGfxState.IntelCapabilities.TranscoderCount ? (IntelGfxState.IntelCapabilities.TranscoderCount - 1) : 0;
    }

    DEBUG(TEXT("Pipe=%u OutputPort=%x OutputType=%u Transcoder=%u"),
        Program->PipeIndex,
        Program->OutputPortMask,
        Program->OutputType,
        Program->TranscoderIndex);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxCaptureModeSnapshot(U32 PipeIndex, LPINTEL_GFX_MODE_SNAPSHOT SnapshotOut) {
    U32 PortIndex = 0;

    if (SnapshotOut == NULL || PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    *SnapshotOut = (INTEL_GFX_MODE_SNAPSHOT){0};
    SnapshotOut->PipeIndex = PipeIndex;

    if (!(IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &SnapshotOut->PipeConf) &&
            IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &SnapshotOut->PipeSource) &&
            IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &SnapshotOut->PipeHTotal) &&
            IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &SnapshotOut->PipeHBlank) &&
            IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &SnapshotOut->PipeHSync) &&
            IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &SnapshotOut->PipeVTotal) &&
            IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &SnapshotOut->PipeVBlank) &&
            IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &SnapshotOut->PipeVSync) &&
            IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &SnapshotOut->PlaneControl) &&
            IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &SnapshotOut->PlaneStride) &&
            IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &SnapshotOut->PlaneSurface))) {
        return DF_RETURN_UNEXPECTED;
    }

    SnapshotOut->OutputPortMask = IntelGfxState.ActiveOutputPortMask;
    if (SnapshotOut->OutputPortMask != 0 && IntelGfxPortIndexFromMask(SnapshotOut->OutputPortMask, &PortIndex)) {
        if (PortIndex < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
            if (IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[PortIndex], &SnapshotOut->LinkControlValue)) {
                // Captured for rollback.
            }
        }
    }

    if (IntelGfxState.ActiveTranscoderIndex < sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0])) {
        (void)IntelGfxReadRegister32Safe(
            IntelTranscoderDdiRegisters[IntelGfxState.ActiveTranscoderIndex], &SnapshotOut->TranscoderControl);
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_DPLL_CTRL1, &SnapshotOut->ClockControlValue)) {
        SnapshotOut->ClockControlRegister = INTEL_REG_DPLL_CTRL1;
    }

    {
        const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
        if (Family != NULL && Family->CompressionControlRegister != 0) {
            U32 CompressionValue = 0;
            if (IntelGfxReadRegister32Safe(Family->CompressionControlRegister, &CompressionValue)) {
                SnapshotOut->CompressionControlRegister = Family->CompressionControlRegister;
                SnapshotOut->CompressionControlValue = CompressionValue;
            }
        }
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_PP_CONTROL, &SnapshotOut->PanelPowerValue)) {
        SnapshotOut->PanelPowerRegister = INTEL_REG_PP_CONTROL;
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_BLC_PWM_CTL2, &SnapshotOut->BacklightValue)) {
        SnapshotOut->BacklightRegister = INTEL_REG_BLC_PWM_CTL2;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxRestoreModeSnapshot(LPINTEL_GFX_MODE_SNAPSHOT Snapshot) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 PipeIndex = 0;
    U32 PortIndex = 0;
    BOOL ExpectedEnabled = FALSE;

    if (Snapshot == NULL) {
        return DF_RETURN_UNEXPECTED;
    }
    if (Family == NULL) {
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    PipeIndex = Snapshot->PipeIndex;
    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    (void)IntelGfxDisablePipe(PipeIndex);

    if (!IntelGfxWriteVerifyRegister32(IntelPipeHTotalRegisters[PipeIndex], Snapshot->PipeHTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHBlankRegisters[PipeIndex], Snapshot->PipeHBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHSyncRegisters[PipeIndex], Snapshot->PipeHSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVTotalRegisters[PipeIndex], Snapshot->PipeVTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVBlankRegisters[PipeIndex], Snapshot->PipeVBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVSyncRegisters[PipeIndex], Snapshot->PipeVSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeSourceRegisters[PipeIndex], Snapshot->PipeSource, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneStrideRegisters[PipeIndex], Snapshot->PlaneStride, Family->StrideWriteMask) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneSurfaceRegisters[PipeIndex], Snapshot->PlaneSurface, INTEL_SURFACE_ALIGN_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Snapshot->OutputPortMask != 0 && IntelGfxPortIndexFromMask(Snapshot->OutputPortMask, &PortIndex) &&
        PortIndex < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
        U32 LinkValue = 0;
        if (IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[PortIndex], &LinkValue)) {
            if (!IntelGfxWriteVerifyRegister32(
                    IntelDdiBufferControlRegisters[PortIndex], Snapshot->LinkControlValue, INTEL_DDI_BUF_CTL_ENABLE)) {
                return DF_RETURN_UNEXPECTED;
            }
        }
    }

    if (IntelGfxState.ActiveTranscoderIndex < sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0]) &&
        Snapshot->TranscoderControl != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                IntelTranscoderDdiRegisters[IntelGfxState.ActiveTranscoderIndex],
                Snapshot->TranscoderControl,
                INTEL_TRANS_DDI_FUNC_ENABLE | INTEL_TRANS_DDI_FUNC_PORT_MASK)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->ClockControlRegister != 0 && Snapshot->ClockControlValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(Snapshot->ClockControlRegister, Snapshot->ClockControlValue, MAX_U32)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->CompressionControlRegister != 0) {
        const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
        U32 CompressionMask = (Family != NULL) ? Family->CompressionControlEnableMask : MAX_U32;

        if (!IntelGfxWriteVerifyRegister32(
                Snapshot->CompressionControlRegister,
                Snapshot->CompressionControlValue,
                CompressionMask)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->PanelPowerRegister != 0 && Snapshot->PanelPowerValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                Snapshot->PanelPowerRegister, Snapshot->PanelPowerValue, INTEL_PANEL_POWER_TARGET_ON)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->BacklightRegister != 0 && Snapshot->BacklightValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                Snapshot->BacklightRegister, Snapshot->BacklightValue, INTEL_BACKLIGHT_PWM_ENABLE)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (!IntelGfxWriteVerifyRegister32(IntelPipeConfRegisters[PipeIndex], Snapshot->PipeConf, INTEL_PIPE_CONF_ENABLE)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWriteVerifyRegister32(
            IntelPlaneControlRegisters[PipeIndex],
            Snapshot->PlaneControl,
            INTEL_PLANE_CTL_ENABLE | INTEL_PLANE_CTL_FORMAT_MASK | INTEL_PLANE_CTL_TILING_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    ExpectedEnabled = (Snapshot->PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
    if (!IntelGfxWaitPipeState(PipeIndex, ExpectedEnabled)) {
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxBuildModeProgram(LPGRAPHICS_MODE_INFO Info, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 RequestedWidth = 0;
    U32 RequestedHeight = 0;
    U32 RequestedBitsPerPixel = 0;
    BOOL HasActiveMode = FALSE;

    if (Info == NULL || ProgramOut == NULL) {
        return DF_RETURN_GENERIC;
    }

    *ProgramOut = (INTEL_GFX_MODE_PROGRAM){0};

    if (Family == NULL) {
        ERROR(TEXT("Unsupported display family (displayVersion=%u)"),
            IntelGfxState.IntelCapabilities.DisplayVersion);
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    RequestedWidth = Info->Width ? Info->Width : IntelGfxState.ActiveWidth;
    RequestedHeight = Info->Height ? Info->Height : IntelGfxState.ActiveHeight;
    RequestedBitsPerPixel = Info->BitsPerPixel ? Info->BitsPerPixel : 32;
    HasActiveMode = IntelGfxState.HasActiveMode;

    if (RequestedWidth == 0 || RequestedHeight == 0) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (RequestedWidth > IntelGfxState.Capabilities.MaxWidth || RequestedHeight > IntelGfxState.Capabilities.MaxHeight) {
        WARNING(TEXT("Requested mode outside capabilities (%ux%u max=%ux%u)"),
            RequestedWidth,
            RequestedHeight,
            IntelGfxState.Capabilities.MaxWidth,
            IntelGfxState.Capabilities.MaxHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (RequestedBitsPerPixel != 32) {
        WARNING(TEXT("Unsupported pixel format bpp=%u"), RequestedBitsPerPixel);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (HasActiveMode != FALSE && (RequestedWidth != IntelGfxState.ActiveWidth || RequestedHeight != IntelGfxState.ActiveHeight)) {
        WARNING(TEXT("Conservative path supports active mode only (%ux%u requested=%ux%u)"),
            IntelGfxState.ActiveWidth,
            IntelGfxState.ActiveHeight,
            RequestedWidth,
            RequestedHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (HasActiveMode != FALSE) {
        if (!IntelGfxReadModeProgram(IntelGfxState.ActivePipeIndex, ProgramOut)) {
            ERROR(TEXT("Failed to read active pipe programming"));
            return DF_RETURN_UNEXPECTED;
        }
    } else {
        U32 PipeIndex = 0;
        U32 HorizontalTotal = 0;
        U32 HorizontalBlankStart = 0;
        U32 HorizontalSyncStart = 0;
        U32 HorizontalSyncEnd = 0;
        U32 VerticalTotal = 0;
        U32 VerticalBlankStart = 0;
        U32 VerticalSyncStart = 0;
        U32 VerticalSyncEnd = 0;

        ProgramOut->PipeIndex = 0;
        if (IntelGfxState.IntelCapabilities.PipeCount != 0 && ProgramOut->PipeIndex >= IntelGfxState.IntelCapabilities.PipeCount) {
            ProgramOut->PipeIndex = IntelGfxState.IntelCapabilities.PipeCount - 1;
        }

        PipeIndex = ProgramOut->PipeIndex;
        if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
            return DF_RETURN_UNEXPECTED;
        }

        if (Family->SupportsColdModeset == FALSE) {
            ERROR(TEXT("Family %s does not support cold modeset"), Family->Name);
            return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
        }

        ProgramOut->PipeConf = 0;
        ProgramOut->PipeSource = ((RequestedHeight - 1) << 16) | (RequestedWidth - 1);
        ProgramOut->PlaneControl = IntelGfxBuildPlaneControl(0);
        ProgramOut->PlaneStride = IntelGfxBuildProgramStride(RequestedWidth, RequestedBitsPerPixel);
        ProgramOut->PlaneSurface = Family->ColdSurfaceOffset & INTEL_SURFACE_ALIGN_MASK;
        ProgramOut->OutputPortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
        ProgramOut->OutputType = IntelGfxResolveOutputTypeFromPort(ProgramOut->OutputPortMask);
        ProgramOut->TranscoderIndex = 0;

        HorizontalTotal = RequestedWidth + INTEL_MODESET_HBLANK_EXTRA;
        HorizontalBlankStart = RequestedWidth;
        HorizontalSyncStart = HorizontalBlankStart + INTEL_MODESET_HSYNC_START_OFFSET;
        HorizontalSyncEnd = HorizontalSyncStart + INTEL_MODESET_HSYNC_PULSE_WIDTH;
        VerticalTotal = RequestedHeight + INTEL_MODESET_VBLANK_EXTRA;
        VerticalBlankStart = RequestedHeight;
        VerticalSyncStart = VerticalBlankStart + INTEL_MODESET_VSYNC_START_OFFSET;
        VerticalSyncEnd = VerticalSyncStart + INTEL_MODESET_VSYNC_PULSE_WIDTH;

        ProgramOut->PipeHTotal = ((HorizontalTotal - 1) << 16) | (RequestedWidth - 1);
        ProgramOut->PipeHBlank = ((HorizontalTotal - 1) << 16) | (HorizontalBlankStart - 1);
        ProgramOut->PipeHSync = ((HorizontalSyncEnd - 1) << 16) | (HorizontalSyncStart - 1);
        ProgramOut->PipeVTotal = ((VerticalTotal - 1) << 16) | (RequestedHeight - 1);
        ProgramOut->PipeVBlank = ((VerticalTotal - 1) << 16) | (VerticalBlankStart - 1);
        ProgramOut->PipeVSync = ((VerticalSyncEnd - 1) << 16) | (VerticalSyncStart - 1);

        DEBUG(TEXT("Cold modeset bootstrap prepared (%ux%u pipe=%u port=%x)"),
            RequestedWidth,
            RequestedHeight,
            ProgramOut->PipeIndex,
            ProgramOut->OutputPortMask);
    }

    ProgramOut->Width = RequestedWidth;
    ProgramOut->Height = RequestedHeight;
    ProgramOut->BitsPerPixel = RequestedBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;
    ProgramOut->PipeSource = ((RequestedHeight - 1) << 16) | (RequestedWidth - 1);
    if (HasActiveMode != FALSE) {
        ProgramOut->PlaneStride = IntelGfxBuildProgramStride(RequestedWidth, RequestedBitsPerPixel);
        ProgramOut->PlaneSurface = IntelGfxState.ActiveSurfaceOffset & INTEL_SURFACE_ALIGN_MASK;
    }
    ProgramOut->PlaneControl = IntelGfxBuildPlaneControl(ProgramOut->PlaneControl);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramMode(LPINTEL_GFX_MODE_PROGRAM Program) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    INTEL_GFX_MODE_SNAPSHOT Snapshot;
    UINT CompletedStages = 0;
    UINT Result = DF_RETURN_SUCCESS;
    BOOL HasSnapshot = FALSE;

    if (Program == NULL) {
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VALIDATE;
        IntelGfxState.LastModesetFailureCode = DF_RETURN_UNEXPECTED;
        return DF_RETURN_UNEXPECTED;
    }

    if (Family == NULL) {
        ERROR(TEXT("Unsupported display family (displayVersion=%u)"),
            IntelGfxState.IntelCapabilities.DisplayVersion);
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VALIDATE;
        IntelGfxState.LastModesetFailureCode = DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    if (IntelGfxState.HasActiveMode == FALSE && Family->SupportsColdModeset == FALSE) {
        ERROR(TEXT("Family %s does not support cold modeset"), Family->Name);
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VALIDATE;
        IntelGfxState.LastModesetFailureCode = DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    Result = IntelGfxCaptureModeSnapshot(Program->PipeIndex, &Snapshot);
    if (Result != DF_RETURN_SUCCESS) {
        if (IntelGfxState.HasActiveMode != FALSE) {
            ERROR(TEXT("Snapshot capture failed"));
            IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VALIDATE;
            IntelGfxState.LastModesetFailureCode = Result;
            return Result;
        }

        WARNING(TEXT("Snapshot capture unavailable, continuing without rollback baseline"));
    } else {
        HasSnapshot = TRUE;
    }

    Result = IntelGfxSelectPipeOutputRouting(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Routing policy failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_ROUTE;
        IntelGfxState.LastModesetFailureCode = Result;
        return Result;
    }

    Result = IntelGfxDisablePipe(Program->PipeIndex);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage disable failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_DISABLE;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_DISABLE_PIPE;

    Result = IntelGfxProgramTranscoderRoute(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage transcoder routing failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_ROUTE;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_ROUTE_TRANSCODER;

    Result = IntelGfxProgramClockSource(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage clock programming failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_CLOCK;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_PROGRAM_CLOCK;

    Result = IntelGfxConfigureConnectorLink(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage link configuration failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_LINK;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_CONFIGURE_LINK;

    Result = IntelGfxPrepareScanoutMemory(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage scanout memory preparation failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_SCANOUT;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_SCANOUT_MEMORY;

    Result = IntelGfxDisableCompressionBlocks(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage compression disable failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_COMPRESSION;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_DISABLE_COMPRESSION;

    Result = IntelGfxEnablePipe(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage pipe enable failed"));
        IntelGfxDumpPipeRegisters(Program->PipeIndex, TEXT("ProgramMode-enable-failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_ENABLE;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_ENABLE_PIPE;

    Result = IntelGfxProgramPanelStability(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage panel stabilization failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_PANEL;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_PANEL_STABILITY;

    Result = IntelGfxVerifyProgramMode(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage verify failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VERIFY;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }

    Result = IntelGfxCommitProgramMode(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Stage commit failed"));
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_COMMIT;
        IntelGfxState.LastModesetFailureCode = Result;
        goto rollback;
    }

    DEBUG(TEXT("Pipe=%u Mode=%ux%u bpp=%u refresh=%u Port=%x Transcoder=%u Stages=%x"),
        Program->PipeIndex,
        Program->Width,
        Program->Height,
        Program->BitsPerPixel,
        Program->RefreshRate,
        Program->OutputPortMask,
        Program->TranscoderIndex,
        CompletedStages);
    IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_NONE;
    IntelGfxState.LastModesetFailureCode = DF_RETURN_SUCCESS;

    return DF_RETURN_SUCCESS;

rollback:
    if (CompletedStages != 0 && HasSnapshot != FALSE) {
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_ROLLBACK;
        UINT RollbackResult = IntelGfxRestoreModeSnapshot(&Snapshot);
        if (RollbackResult != DF_RETURN_SUCCESS) {
            ERROR(TEXT("Rollback failed stageMask=%x result=%u"), CompletedStages, RollbackResult);
            IntelGfxState.LastModesetFailureCode = RollbackResult;
        } else {
            WARNING(TEXT("Rollback completed stageMask=%x"), CompletedStages);
        }
    }
    IntelGfxDumpPipeRegisters(Program->PipeIndex, TEXT("ProgramMode-after-rollback"));

    return Result;
}

/************************************************************************/

static BOOL IntelGfxMapActiveFrameBuffer(void) {
    U32 Bar2Raw = 0;
    U32 Bar2Base = 0;
    U32 Bar2Size = 0;

    if (IntelGfxState.Device == NULL) {
        return FALSE;
    }

    Bar2Raw = IntelGfxState.Device->Info.BAR[2];
    if (PCI_BAR_IS_IO(Bar2Raw)) {
        ERROR(TEXT("BAR2 is I/O (bar2=%x)"), Bar2Raw);
        return FALSE;
    }

    Bar2Base = PCI_GetBARBase(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    Bar2Size = PCI_GetBARSize(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    if (Bar2Base == 0 || Bar2Size == 0) {
        ERROR(TEXT("Invalid BAR2 base=%x size=%u"), Bar2Base, Bar2Size);
        return FALSE;
    }

    IntelGfxState.FrameBufferSize = IntelGfxState.ActiveStride * IntelGfxState.ActiveHeight;
    if (IntelGfxState.FrameBufferSize == 0) {
        ERROR(TEXT("Invalid frame buffer size"));
        return FALSE;
    }

    if (IntelGfxState.ActiveSurfaceOffset >= Bar2Size) {
        ERROR(TEXT("Surface offset out of BAR2 range (offset=%x size=%u)"),
            IntelGfxState.ActiveSurfaceOffset,
            Bar2Size);
        return FALSE;
    }

    if (IntelGfxState.FrameBufferSize > (Bar2Size - IntelGfxState.ActiveSurfaceOffset)) {
        ERROR(TEXT("Frame buffer exceeds BAR2 window (size=%u available=%u)"),
            IntelGfxState.FrameBufferSize,
            Bar2Size - IntelGfxState.ActiveSurfaceOffset);
        return FALSE;
    }

    IntelGfxState.FrameBufferPhysical = (PHYSICAL)(Bar2Base + IntelGfxState.ActiveSurfaceOffset);
    IntelGfxState.FrameBufferLinear = MapIOMemory(IntelGfxState.FrameBufferPhysical, IntelGfxState.FrameBufferSize);
    if (IntelGfxState.FrameBufferLinear == 0) {
        ERROR(TEXT("MapIOMemory failed for base=%p size=%u"),
            (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical,
            IntelGfxState.FrameBufferSize);
        return FALSE;
    }

    DEBUG(TEXT("FrameBuffer=%p size=%u stride=%u"),
        (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical,
        IntelGfxState.FrameBufferSize,
        IntelGfxState.ActiveStride);

    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxBuildTakeoverContext(void) {
    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.ActiveWidth == 0 || IntelGfxState.ActiveHeight == 0) {
        return FALSE;
    }

    IntelGfxState.Context = (GRAPHICSCONTEXT){
        .TypeID = KOID_GRAPHICSCONTEXT,
        .References = 1,
        .Mutex = EMPTY_MUTEX,
        .Driver = &IntelGfxDriver,
        .Width = (I32)IntelGfxState.ActiveWidth,
        .Height = (I32)IntelGfxState.ActiveHeight,
        .BitsPerPixel = IntelGfxState.ActiveBitsPerPixel,
        .BytesPerScanLine = IntelGfxState.ActiveStride,
        .RedPosition = 16,
        .RedMaskSize = 8,
        .GreenPosition = 8,
        .GreenMaskSize = 8,
        .BluePosition = 0,
        .BlueMaskSize = 8,
        .MemoryBase = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear,
        .LoClip = {.X = 0, .Y = 0},
        .HiClip = {.X = (I32)IntelGfxState.ActiveWidth - 1, .Y = (I32)IntelGfxState.ActiveHeight - 1},
        .Origin = {.X = 0, .Y = 0},
        .RasterOperation = ROP_SET
    };

    return TRUE;
}

/************************************************************************/

UINT IntelGfxTakeoverActiveMode(void) {
    if (!IntelGfxReadActiveScanoutState()) {
        ERROR(TEXT("No active Intel scanout state found"));
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_NO_ACTIVE_SCANOUT;
    }

    if (!IntelGfxMapActiveFrameBuffer()) {
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_MAP_FRAMEBUFFER_FAILED;
    }

    if (!IntelGfxBuildTakeoverContext()) {
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_BUILD_CONTEXT_FAILED;
    }

    IntelGfxState.HasActiveMode = TRUE;
    IntelGfxOnModeActivated();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxSetMode(LPGRAPHICS_MODE_INFO Info) {
    INTEL_GFX_MODE_PROGRAM Program;
    UINT Result = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    Result = IntelGfxBuildModeProgram(Info, &Program);
    if (Result != DF_RETURN_SUCCESS) {
        IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_VALIDATE;
        IntelGfxState.LastModesetFailureCode = Result;
        return Result;
    }

    Result = IntelGfxProgramMode(&Program);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    IntelGfxReleaseAllSurfaces();
    IntelGfxState.PresentBlitCount = 0;

    if (IntelGfxState.FrameBufferLinear != 0 && IntelGfxState.FrameBufferSize != 0) {
        UnMapIOMemory(IntelGfxState.FrameBufferLinear, IntelGfxState.FrameBufferSize);
        IntelGfxState.FrameBufferLinear = 0;
        IntelGfxState.FrameBufferSize = 0;
        IntelGfxState.FrameBufferPhysical = 0;
    }

    Result = IntelGfxTakeoverActiveMode();
    if (Result != DF_RETURN_SUCCESS) {
        Result = IntelGfxCommitProgramMode(&Program);
        if (Result != DF_RETURN_SUCCESS) {
            IntelGfxState.HasActiveMode = FALSE;
            IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_COMMIT;
            IntelGfxState.LastModesetFailureCode = Result;
            return Result;
        }

        Result = IntelGfxMapActiveFrameBuffer() ? DF_RETURN_SUCCESS : DF_RETURN_IGFX_MAP_FRAMEBUFFER_FAILED;
        if (Result != DF_RETURN_SUCCESS) {
            IntelGfxState.HasActiveMode = FALSE;
            IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_COMMIT;
            IntelGfxState.LastModesetFailureCode = Result;
            return Result;
        }

        Result = IntelGfxBuildTakeoverContext() ? DF_RETURN_SUCCESS : DF_RETURN_IGFX_BUILD_CONTEXT_FAILED;
        if (Result != DF_RETURN_SUCCESS) {
            IntelGfxState.HasActiveMode = FALSE;
            IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_COMMIT;
            IntelGfxState.LastModesetFailureCode = Result;
            return Result;
        }

        IntelGfxState.HasActiveMode = TRUE;
        WARNING(TEXT("Takeover refresh unavailable, context rebuilt from programmed cold mode"));
    }

    IntelGfxState.LastModesetFailureStage = INTEL_GFX_MODESET_STAGE_NONE;
    IntelGfxState.LastModesetFailureCode = DF_RETURN_SUCCESS;

    GraphicsDrawTestPattern(&IntelGfxState.Context);

    SAFE_USE(Info) {
        Info->Width = (U32)IntelGfxState.Context.Width;
        Info->Height = (U32)IntelGfxState.Context.Height;
        Info->BitsPerPixel = IntelGfxState.Context.BitsPerPixel;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
