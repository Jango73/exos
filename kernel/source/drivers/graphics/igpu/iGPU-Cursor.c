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


    Intel graphics hardware cursor support

\************************************************************************/

#include "iGPU-Internal.h"

#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Convert one cursor failure reason to concise diagnostics text.
 * @param Reason Failure reason code.
 * @return Static short text for diagnostics.
 */
LPCSTR IntelGfxCursorFailureReasonToText(INTEL_GFX_CURSOR_FAILURE_REASON Reason) {
    switch (Reason) {
        case INTEL_GFX_CURSOR_FAILURE_NONE:
            return TEXT("none");
        case INTEL_GFX_CURSOR_FAILURE_UNSUPPORTED_GENERATION:
            return TEXT("unsupported_generation");
        case INTEL_GFX_CURSOR_FAILURE_INVALID_PIPE:
            return TEXT("invalid_pipe");
        case INTEL_GFX_CURSOR_FAILURE_UNSUPPORTED_FORMAT:
            return TEXT("unsupported_format");
        case INTEL_GFX_CURSOR_FAILURE_INVALID_SHAPE:
            return TEXT("invalid_shape");
        case INTEL_GFX_CURSOR_FAILURE_DMA_ALLOCATION:
            return TEXT("dma_allocation");
        case INTEL_GFX_CURSOR_FAILURE_REGISTER_ACCESS:
            return TEXT("register_access");
    }

    return TEXT("unknown");
}

/************************************************************************/

/**
 * @brief Check whether the conservative hardware cursor path is supported.
 * @return TRUE when the backend may expose the cursor plane path.
 */
static BOOL IntelGfxCursorIsSupported(void) {
    return (IntelGfxState.IntelCapabilities.Generation >= 5) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Resolve cursor MMIO registers for one active pipe.
 * @param PipeIndex Active pipe index.
 * @param ControlRegisterOut Receives control register offset.
 * @param BaseRegisterOut Receives base register offset.
 * @param PositionRegisterOut Receives position register offset.
 * @return TRUE on success.
 */
static BOOL IntelGfxCursorResolveRegisters(
    U32 PipeIndex,
    U32* ControlRegisterOut,
    U32* BaseRegisterOut,
    U32* PositionRegisterOut) {
    if (ControlRegisterOut == NULL || BaseRegisterOut == NULL || PositionRegisterOut == NULL) {
        return FALSE;
    }

    switch (PipeIndex) {
        case 0:
            *ControlRegisterOut = INTEL_REG_CURSOR_A_CONTROL;
            *BaseRegisterOut = INTEL_REG_CURSOR_A_BASE;
            *PositionRegisterOut = INTEL_REG_CURSOR_A_POSITION;
            return TRUE;
        case 1:
            *ControlRegisterOut = INTEL_REG_CURSOR_B_CONTROL;
            *BaseRegisterOut = INTEL_REG_CURSOR_B_BASE;
            *PositionRegisterOut = INTEL_REG_CURSOR_B_POSITION;
            return TRUE;
        case 2:
            *ControlRegisterOut = INTEL_REG_CURSOR_C_CONTROL;
            *BaseRegisterOut = INTEL_REG_CURSOR_C_BASE;
            *PositionRegisterOut = INTEL_REG_CURSOR_C_POSITION;
            return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Update stored cursor failure diagnostics.
 * @param Reason Failure reason.
 * @param Status Driver status associated with the failure.
 */
static void IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_REASON Reason, UINT Status) {
    IntelGfxState.CursorLastFailureReason = Reason;
    IntelGfxState.CursorLastCommandStatus = Status;
}

/************************************************************************/

/**
 * @brief Ensure the 64x64 ARGB cursor DMA buffer exists.
 * @return TRUE on success.
 */
static BOOL IntelGfxCursorEnsureBuffer(void) {
    UINT RequiredSize = INTEL_CURSOR_MAX_WIDTH * INTEL_CURSOR_MAX_HEIGHT * INTEL_CURSOR_BYTES_PER_PIXEL;

    if (IntelGfxState.CursorBuffer.LinearBase != 0) {
        return TRUE;
    }

    if (!DMABufferAllocate(&IntelGfxState.CursorBuffer, RequiredSize, TRUE, TEXT("IntelGfxCursor"))) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_DMA_ALLOCATION, DF_RETURN_UNEXPECTED);
        ERROR(TEXT("[IntelGfxCursorEnsureBuffer] DMABufferAllocate failed size=%u"), RequiredSize);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Copy one ARGB cursor image into the conservative hardware buffer.
 * @param Info Cursor shape parameters.
 * @return TRUE on success.
 */
static BOOL IntelGfxCursorUploadShape(LPGFX_CURSOR_SHAPE_INFO Info) {
    U8* Destination = NULL;
    const U8* Source = NULL;
    U32 Row = 0;
    U32 CopyBytes = 0;
    U32 DestinationPitch = INTEL_CURSOR_MAX_WIDTH * INTEL_CURSOR_BYTES_PER_PIXEL;

    if (Info == NULL || Info->Pixels == NULL) {
        return FALSE;
    }

    if (!IntelGfxCursorEnsureBuffer()) {
        return FALSE;
    }

    Destination = (U8*)(LINEAR)IntelGfxState.CursorBuffer.LinearBase;
    Source = (const U8*)Info->Pixels;
    CopyBytes = Info->Width * INTEL_CURSOR_BYTES_PER_PIXEL;

    MemorySet(Destination, 0, IntelGfxState.CursorBuffer.AllocatedSize);
    for (Row = 0; Row < Info->Height; Row++) {
        MemoryCopy(
            Destination + (Row * DestinationPitch),
            Source + (Row * Info->Pitch),
            CopyBytes);
    }

    IntelGfxState.CursorWidth = Info->Width;
    IntelGfxState.CursorHeight = Info->Height;
    IntelGfxState.CursorHotspotX = Info->HotspotX;
    IntelGfxState.CursorHotspotY = Info->HotspotY;
    IntelGfxState.CursorShapeLoaded = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Encode one signed cursor origin coordinate for Intel cursor registers.
 * @param Value Signed origin coordinate.
 * @param SignMask Sign bit mask for the target axis.
 * @return Encoded coordinate value.
 */
static U32 IntelGfxCursorEncodeCoordinate(I32 Value, U32 SignMask) {
    U32 Magnitude = 0;

    if (Value < 0) {
        Magnitude = (U32)(-Value);
        if (Magnitude > INTEL_CURSOR_POSITION_COORDINATE_MASK) {
            Magnitude = INTEL_CURSOR_POSITION_COORDINATE_MASK;
        }

        return Magnitude | SignMask;
    }

    Magnitude = (U32)Value;
    if (Magnitude > INTEL_CURSOR_POSITION_COORDINATE_MASK) {
        Magnitude = INTEL_CURSOR_POSITION_COORDINATE_MASK;
    }

    return Magnitude;
}

/************************************************************************/

/**
 * @brief Build one cursor position register payload from stored logical coordinates.
 * @return Encoded MMIO position value.
 */
static U32 IntelGfxCursorBuildPositionRegisterValue(void) {
    I32 OriginX = IntelGfxState.CursorX - (I32)IntelGfxState.CursorHotspotX;
    I32 OriginY = IntelGfxState.CursorY - (I32)IntelGfxState.CursorHotspotY;
    U32 EncodedX = IntelGfxCursorEncodeCoordinate(OriginX, INTEL_CURSOR_POSITION_X_SIGN);
    U32 EncodedY = IntelGfxCursorEncodeCoordinate(OriginY, INTEL_CURSOR_POSITION_Y_SIGN);

    return EncodedX | (EncodedY << 16);
}

/************************************************************************/

/**
 * @brief Program or disable the hardware cursor plane from stored state.
 * @return DF_RETURN_SUCCESS on success.
 */
static UINT IntelGfxCursorApplyStateLocked(void) {
    U32 ControlRegister = 0;
    U32 BaseRegister = 0;
    U32 PositionRegister = 0;
    U32 BaseValue = 0;
    U32 PositionValue = 0;

    if (!IntelGfxCursorIsSupported()) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_UNSUPPORTED_GENERATION, DF_RETURN_NOT_IMPLEMENTED);
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (!IntelGfxState.HasActiveMode) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_NONE, DF_RETURN_SUCCESS);
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxCursorResolveRegisters(
            IntelGfxState.ActivePipeIndex,
            &ControlRegister,
            &BaseRegister,
            &PositionRegister)) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_INVALID_PIPE, DF_RETURN_UNEXPECTED);
        WARNING(TEXT("[IntelGfxCursorApplyStateLocked] Unsupported pipe=%u"), IntelGfxState.ActivePipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxState.HasActiveMode || !IntelGfxState.CursorShapeLoaded || !IntelGfxState.CursorVisible) {
        if (!IntelGfxWriteMmio32(ControlRegister, INTEL_CURSOR_CONTROL_MODE_DISABLE)) {
            IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_REGISTER_ACCESS, DF_RETURN_UNEXPECTED);
            WARNING(TEXT("[IntelGfxCursorApplyStateLocked] Disable failed pipe=%u"), IntelGfxState.ActivePipeIndex);
            return DF_RETURN_UNEXPECTED;
        }

        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_NONE, DF_RETURN_SUCCESS);
        return DF_RETURN_SUCCESS;
    }

    BaseValue = (U32)(DMABufferGetPhysical(&IntelGfxState.CursorBuffer, 0) & INTEL_SURFACE_ALIGN_MASK);
    PositionValue = IntelGfxCursorBuildPositionRegisterValue();

    if (BaseValue == 0) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_DMA_ALLOCATION, DF_RETURN_UNEXPECTED);
        WARNING(TEXT("[IntelGfxCursorApplyStateLocked] Cursor physical base unavailable"));
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWriteMmio32(ControlRegister, INTEL_CURSOR_CONTROL_MODE_DISABLE) ||
        !IntelGfxWriteMmio32(BaseRegister, BaseValue) ||
        !IntelGfxWriteMmio32(PositionRegister, PositionValue) ||
        !IntelGfxWriteMmio32(ControlRegister, INTEL_CURSOR_CONTROL_MODE_64_ARGB)) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_REGISTER_ACCESS, DF_RETURN_UNEXPECTED);
        WARNING(TEXT("[IntelGfxCursorApplyStateLocked] Program failed pipe=%u base=%x pos=%x"),
            IntelGfxState.ActivePipeIndex,
            BaseValue,
            PositionValue);
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_NONE, DF_RETURN_SUCCESS);
    DEBUG(TEXT("[IntelGfxCursorApplyStateLocked] path=hardware pipe=%u size=%ux%u hotspot=%u,%u visible=%u"),
        IntelGfxState.ActivePipeIndex,
        IntelGfxState.CursorWidth,
        IntelGfxState.CursorHeight,
        IntelGfxState.CursorHotspotX,
        IntelGfxState.CursorHotspotY,
        IntelGfxState.CursorVisible ? 1 : 0);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Release hardware cursor resources and stored state.
 */
void IntelGfxCursorReleaseResources(void) {
    U32 ControlRegister = 0;
    U32 BaseRegister = 0;
    U32 PositionRegister = 0;

    UNUSED(BaseRegister);
    UNUSED(PositionRegister);

    if (IntelGfxState.HasActiveMode &&
        IntelGfxCursorResolveRegisters(
            IntelGfxState.ActivePipeIndex,
            &ControlRegister,
            &BaseRegister,
            &PositionRegister)) {
        (void)IntelGfxWriteMmio32(ControlRegister, INTEL_CURSOR_CONTROL_MODE_DISABLE);
    }

    if (IntelGfxState.CursorBuffer.LinearBase != 0) {
        DMABufferRelease(&IntelGfxState.CursorBuffer);
    }

    IntelGfxState.CursorWidth = 0;
    IntelGfxState.CursorHeight = 0;
    IntelGfxState.CursorHotspotX = 0;
    IntelGfxState.CursorHotspotY = 0;
    IntelGfxState.CursorX = 0;
    IntelGfxState.CursorY = 0;
    IntelGfxState.CursorVisible = FALSE;
    IntelGfxState.CursorShapeLoaded = FALSE;
    IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_NONE, DF_RETURN_SUCCESS);
}

/************************************************************************/

/**
 * @brief Reapply stored hardware cursor state after one mode activation.
 */
void IntelGfxCursorOnModeActivated(void) {
    UINT Status = DF_RETURN_SUCCESS;

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    Status = IntelGfxCursorApplyStateLocked();
    UnlockMutex(&(IntelGfxState.PresentMutex));

    if (Status != DF_RETURN_SUCCESS && Status != DF_RETURN_NOT_IMPLEMENTED) {
        WARNING(TEXT("[IntelGfxCursorOnModeActivated] Reapply failed status=%u reason=%s"),
            Status,
            IntelGfxCursorFailureReasonToText(IntelGfxState.CursorLastFailureReason));
    }
}

/************************************************************************/

/**
 * @brief Store one new cursor shape and program it when possible.
 * @param Info Cursor shape parameters.
 * @return DF_RETURN_SUCCESS on success.
 */
UINT IntelGfxCursorSetShape(LPGFX_CURSOR_SHAPE_INFO Info) {
    UINT Status = DF_RETURN_SUCCESS;

    if (Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Info->Header.Size < sizeof(GFX_CURSOR_SHAPE_INFO) || Info->Pixels == NULL) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_INVALID_SHAPE, DF_RETURN_BAD_PARAMETER);
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Info->Format != GFX_CURSOR_FORMAT_ARGB8888) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_UNSUPPORTED_FORMAT, DF_RETURN_NOT_IMPLEMENTED);
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (Info->Width == 0 ||
        Info->Height == 0 ||
        Info->Width > INTEL_CURSOR_MAX_WIDTH ||
        Info->Height > INTEL_CURSOR_MAX_HEIGHT ||
        Info->HotspotX >= Info->Width ||
        Info->HotspotY >= Info->Height ||
        Info->Pitch < (Info->Width * INTEL_CURSOR_BYTES_PER_PIXEL)) {
        IntelGfxCursorSetFailure(INTEL_GFX_CURSOR_FAILURE_INVALID_SHAPE, DF_RETURN_BAD_PARAMETER);
        return DF_RETURN_BAD_PARAMETER;
    }

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    if (!IntelGfxCursorUploadShape(Info)) {
        Status = IntelGfxState.CursorLastCommandStatus;
    } else if (IntelGfxState.HasActiveMode) {
        Status = IntelGfxCursorApplyStateLocked();
    }
    UnlockMutex(&(IntelGfxState.PresentMutex));

    return Status;
}

/************************************************************************/

/**
 * @brief Update one logical cursor position and program it when possible.
 * @param Info Cursor position parameters.
 * @return DF_RETURN_SUCCESS on success.
 */
UINT IntelGfxCursorSetPosition(LPGFX_CURSOR_POSITION_INFO Info) {
    UINT Status = DF_RETURN_SUCCESS;

    if (Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Info->Header.Size < sizeof(GFX_CURSOR_POSITION_INFO)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    IntelGfxState.CursorX = Info->X;
    IntelGfxState.CursorY = Info->Y;
    if (IntelGfxState.HasActiveMode && IntelGfxState.CursorShapeLoaded && IntelGfxState.CursorVisible) {
        Status = IntelGfxCursorApplyStateLocked();
    }
    UnlockMutex(&(IntelGfxState.PresentMutex));

    return Status;
}

/************************************************************************/

/**
 * @brief Update hardware cursor visibility and program it when possible.
 * @param Info Cursor visibility parameters.
 * @return DF_RETURN_SUCCESS on success.
 */
UINT IntelGfxCursorSetVisible(LPGFX_CURSOR_VISIBLE_INFO Info) {
    UINT Status = DF_RETURN_SUCCESS;

    if (Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Info->Header.Size < sizeof(GFX_CURSOR_VISIBLE_INFO)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    IntelGfxState.CursorVisible = Info->IsVisible;
    if (IntelGfxState.HasActiveMode) {
        Status = IntelGfxCursorApplyStateLocked();
    }
    UnlockMutex(&(IntelGfxState.PresentMutex));

    return Status;
}
