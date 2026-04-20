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


    Intel graphics (modeset verify and commit stages)

\************************************************************************/

#include "iGPU-Internal.h"

#include "system/Clock.h"
#include "log/Log.h"

/************************************************************************/

static const U32 IntelPipeConfRegisters[] = {INTEL_REG_PIPE_A_CONF, INTEL_REG_PIPE_B_CONF, INTEL_REG_PIPE_C_CONF};
static const U32 IntelPipeSourceRegisters[] = {INTEL_REG_PIPE_A_SRC, INTEL_REG_PIPE_B_SRC, INTEL_REG_PIPE_C_SRC};
static const U32 IntelPlaneControlRegisters[] = {INTEL_REG_PLANE_A_CTL, INTEL_REG_PLANE_B_CTL, INTEL_REG_PLANE_C_CTL};
static const U32 IntelPlaneStrideRegisters[] = {INTEL_REG_PLANE_A_STRIDE, INTEL_REG_PLANE_B_STRIDE, INTEL_REG_PLANE_C_STRIDE};
static const U32 IntelPlaneSurfaceRegisters[] = {INTEL_REG_PLANE_A_SURF, INTEL_REG_PLANE_B_SURF, INTEL_REG_PLANE_C_SURF};

/************************************************************************/

static BOOL IntelGfxWaitPipeEnabled(U32 PipeIndex, BOOL ExpectedEnabled) {
    UINT StartTime = 0;
    UINT Loop = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    StartTime = GetSystemTime();
    for (Loop = 0; HasOperationTimedOut(StartTime, Loop, INTEL_MODESET_LOOP_LIMIT, INTEL_MODESET_TIMEOUT_MILLISECONDS) == FALSE; Loop++) {
        U32 PipeConf = 0;
        BOOL Enabled = FALSE;

        if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
            return FALSE;
        }

        Enabled = (PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
        if (Enabled == ExpectedEnabled) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

UINT IntelGfxVerifyProgramMode(const INTEL_GFX_MODE_PROGRAM* Program) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 PipeConf = 0;
    U32 PipeSource = 0;
    U32 PlaneControl = 0;
    U32 PlaneStride = 0;
    U32 PlaneSurface = 0;
    U32 ExpectedPlaneControl = 0;
    U32 DecodedPlaneStride = 0;
    U32 CompressionValue = 0;
    U32 PipeIndex = 0;

    if (Program == NULL || Family == NULL) {
        return DF_RETURN_IGFX_UNSUPPORTED_FAMILY;
    }

    PipeIndex = Program->PipeIndex;
    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWaitPipeEnabled(PipeIndex, TRUE)) {
        ERROR(TEXT("Pipe enable state did not settle (pipe=%u)"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf) ||
        !IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &PipeSource) ||
        !IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl) ||
        !IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &PlaneStride) ||
        !IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &PlaneSurface)) {
        ERROR(TEXT("Register readback failed (pipe=%u)"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    if ((PipeConf & INTEL_PIPE_CONF_ENABLE) == 0) {
        ERROR(TEXT("Pipe disabled after enable (pipe=%u conf=%x)"), PipeIndex, PipeConf);
        return DF_RETURN_UNEXPECTED;
    }

    if ((PlaneControl & INTEL_PLANE_CTL_ENABLE) == 0) {
        ERROR(TEXT("Plane disabled after enable (pipe=%u ctl=%x)"), PipeIndex, PlaneControl);
        return DF_RETURN_UNEXPECTED;
    }

    if (PipeSource != Program->PipeSource) {
        ERROR(TEXT("Pipe source mismatch pipe=%u expected=%x actual=%x"),
            PipeIndex,
            Program->PipeSource,
            PipeSource);
        return DF_RETURN_UNEXPECTED;
    }

    DecodedPlaneStride = IntelGfxResolveStrideFromReadback(PlaneStride, Program->Width, Program->BitsPerPixel);
    if (DecodedPlaneStride != Program->PlaneStride) {
        ERROR(TEXT("Plane stride mismatch pipe=%u expected=%x actual=%x raw=%x"),
            PipeIndex,
            Program->PlaneStride,
            DecodedPlaneStride,
            PlaneStride);
        return DF_RETURN_UNEXPECTED;
    }

    if ((PlaneSurface & INTEL_SURFACE_ALIGN_MASK) != (Program->PlaneSurface & INTEL_SURFACE_ALIGN_MASK)) {
        ERROR(TEXT("Plane surface mismatch pipe=%u expected=%x actual=%x"),
            PipeIndex,
            Program->PlaneSurface,
            PlaneSurface);
        return DF_RETURN_UNEXPECTED;
    }

    ExpectedPlaneControl = IntelGfxBuildPlaneControl(Program->PlaneControl) | INTEL_PLANE_CTL_ENABLE;
    if ((PlaneControl & (INTEL_PLANE_CTL_FORMAT_MASK | INTEL_PLANE_CTL_TILING_MASK)) !=
        (ExpectedPlaneControl & (INTEL_PLANE_CTL_FORMAT_MASK | INTEL_PLANE_CTL_TILING_MASK))) {
        ERROR(TEXT("Plane format/tiling mismatch pipe=%u expected=%x actual=%x"),
            PipeIndex,
            ExpectedPlaneControl,
            PlaneControl);
        return DF_RETURN_UNEXPECTED;
    }

    if (Family->RequireCompressionDisable != FALSE && Family->CompressionControlRegister != 0 &&
        Family->CompressionControlEnableMask != 0) {
        if (IntelGfxReadMmio32(Family->CompressionControlRegister, &CompressionValue)) {
            if ((CompressionValue & Family->CompressionControlEnableMask) != 0) {
                ERROR(TEXT("Compression still enabled reg=%x value=%x mask=%x"),
                    Family->CompressionControlRegister,
                    CompressionValue,
                    Family->CompressionControlEnableMask);
                return DF_RETURN_UNEXPECTED;
            }
        }
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxCommitProgramMode(const INTEL_GFX_MODE_PROGRAM* Program) {
    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxState.ActivePipeIndex = Program->PipeIndex;
    IntelGfxState.ActiveWidth = Program->Width;
    IntelGfxState.ActiveHeight = Program->Height;
    IntelGfxState.ActiveBitsPerPixel = Program->BitsPerPixel;
    IntelGfxState.ActiveStride = Program->PlaneStride;
    IntelGfxState.ActiveSurfaceOffset = Program->PlaneSurface & INTEL_SURFACE_ALIGN_MASK;
    IntelGfxState.ActiveOutputPortMask = Program->OutputPortMask;
    IntelGfxState.ActiveTranscoderIndex = Program->TranscoderIndex;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
