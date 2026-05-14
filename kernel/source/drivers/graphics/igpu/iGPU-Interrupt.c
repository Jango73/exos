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


    Intel graphics (vblank and present synchronization)

\************************************************************************/

#include "iGPU-Internal.h"

#include "system/Clock.h"
#include "log/Log.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define INTEL_GFX_WAIT_VBLANK_LOG_INTERVAL_MS 1000

/************************************************************************/

static RATE_LIMITER IntelGfxVBlankTimeoutLogLimiter = {0};

/************************************************************************/

static const U32 IntelPipeScanlineRegisters[] = {INTEL_REG_PIPE_A_SCANLINE, INTEL_REG_PIPE_B_SCANLINE, INTEL_REG_PIPE_C_SCANLINE};
static const U32 IntelPipeStatusRegisters[] = {INTEL_REG_PIPE_A_STATUS, INTEL_REG_PIPE_B_STATUS, INTEL_REG_PIPE_C_STATUS};

/************************************************************************/

static BOOL IntelGfxReadPipeScanline(U32 PipeIndex, U32* ScanlineOut) {
    U32 Scanline = 0;

    if (ScanlineOut == NULL) {
        return FALSE;
    }

    if (PipeIndex >= sizeof(IntelPipeScanlineRegisters) / sizeof(IntelPipeScanlineRegisters[0])) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeScanlineRegisters[PipeIndex], &Scanline)) {
        return FALSE;
    }

    if (Scanline == MAX_U32) {
        return FALSE;
    }

    *ScanlineOut = Scanline & INTEL_SCANLINE_MASK;
    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxTryEnableVBlankInterrupt(U32 PipeIndex) {
    U32 PipeStatus = 0;
    U32 EnabledValue = 0;

    if (PipeIndex >= sizeof(IntelPipeStatusRegisters) / sizeof(IntelPipeStatusRegisters[0])) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeStatusRegisters[PipeIndex], &PipeStatus)) {
        return FALSE;
    }

    if (PipeStatus == MAX_U32) {
        return FALSE;
    }

    EnabledValue = PipeStatus | INTEL_PIPE_STATUS_VBLANK_INTERRUPT_ENABLE | INTEL_PIPE_STATUS_VBLANK_INTERRUPT;
    if (!IntelGfxWriteMmio32(IntelPipeStatusRegisters[PipeIndex], EnabledValue)) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeStatusRegisters[PipeIndex], &PipeStatus)) {
        return FALSE;
    }

    if (PipeStatus == MAX_U32) {
        return FALSE;
    }

    return (PipeStatus & INTEL_PIPE_STATUS_VBLANK_INTERRUPT_ENABLE) != 0;
}

/************************************************************************/

static BOOL IntelGfxPollVBlankInterrupt(U32 PipeIndex) {
    U32 PipeStatus = 0;

    if (IntelGfxState.VBlankInterruptEnabled == FALSE) {
        return FALSE;
    }

    if (PipeIndex >= sizeof(IntelPipeStatusRegisters) / sizeof(IntelPipeStatusRegisters[0])) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeStatusRegisters[PipeIndex], &PipeStatus)) {
        return FALSE;
    }

    if (PipeStatus == MAX_U32 || (PipeStatus & INTEL_PIPE_STATUS_VBLANK_INTERRUPT) == 0) {
        return FALSE;
    }

    (void)IntelGfxWriteMmio32(IntelPipeStatusRegisters[PipeIndex], PipeStatus | INTEL_PIPE_STATUS_VBLANK_INTERRUPT);
    IntelGfxState.VBlankInterruptCount++;
    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxPollVBlankScanline(U32 PipeIndex) {
    U32 Scanline = 0;
    BOOL Detected = FALSE;

    if (!IntelGfxReadPipeScanline(PipeIndex, &Scanline)) {
        return FALSE;
    }

    if (IntelGfxState.LastVBlankScanline == MAX_U32) {
        IntelGfxState.LastVBlankScanline = Scanline;
        return FALSE;
    }

    if (Scanline < IntelGfxState.LastVBlankScanline) {
        Detected = TRUE;
    }

    IntelGfxState.LastVBlankScanline = Scanline;
    if (Detected) {
        IntelGfxState.VBlankPollCount++;
    }

    return Detected;
}

/************************************************************************/

static BOOL IntelGfxPollVBlankEvent(void) {
    U32 PipeIndex = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0 || IntelGfxState.HasActiveMode == FALSE) {
        return FALSE;
    }

    PipeIndex = IntelGfxState.ActivePipeIndex;
    if (PipeIndex >= sizeof(IntelPipeScanlineRegisters) / sizeof(IntelPipeScanlineRegisters[0])) {
        return FALSE;
    }

    if (IntelGfxPollVBlankInterrupt(PipeIndex)) {
        return TRUE;
    }

    return IntelGfxPollVBlankScanline(PipeIndex);
}

/************************************************************************/

void IntelGfxNotePresentBlit(void) {
    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    IntelGfxState.PresentBlitCount++;
    IntelGfxState.PresentFrameSequence++;
    if (IntelGfxPollVBlankEvent()) {
        IntelGfxState.VBlankFrameSequence++;
    }
    UnlockMutex(&(IntelGfxState.PresentMutex));
}

/************************************************************************/

UINT IntelGfxWaitForNextVBlank(U32 TimeoutMilliseconds, U32* SequenceOut) {
    U32 EffectiveTimeout = TimeoutMilliseconds;
    UINT StartTime = 0;
    UINT Loop = 0;
    U32 InitialSequence = 0;
    U32 CurrentSequence = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0 || IntelGfxState.HasActiveMode == FALSE) {
        return DF_RETURN_UNEXPECTED;
    }

    if (EffectiveTimeout == 0) {
        EffectiveTimeout = INTEL_GFX_WAIT_VBLANK_DEFAULT_TIMEOUT_MS;
    }

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    if (IntelGfxPollVBlankEvent()) {
        IntelGfxState.VBlankFrameSequence++;
    }
    InitialSequence = IntelGfxState.VBlankFrameSequence;
    UnlockMutex(&(IntelGfxState.PresentMutex));

    StartTime = GetSystemTime();
    for (Loop = 0; HasOperationTimedOut(StartTime, Loop, INTEL_MODESET_LOOP_LIMIT, EffectiveTimeout) == FALSE; Loop++) {
        LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
        if (IntelGfxPollVBlankEvent()) {
            IntelGfxState.VBlankFrameSequence++;
        }
        CurrentSequence = IntelGfxState.VBlankFrameSequence;
        UnlockMutex(&(IntelGfxState.PresentMutex));

        if (CurrentSequence != InitialSequence) {
            if (SequenceOut != NULL) {
                *SequenceOut = CurrentSequence;
            }
            return DF_RETURN_SUCCESS;
        }

        Sleep(1);
    }

    if (IntelGfxVBlankTimeoutLogLimiter.Initialized == FALSE) {
        (void)RateLimiterInit(&IntelGfxVBlankTimeoutLogLimiter, 2, INTEL_GFX_WAIT_VBLANK_LOG_INTERVAL_MS);
    }

    {
        U32 Suppressed = 0;
        U32 Now = GetSystemTime();
        if (RateLimiterShouldTrigger(&IntelGfxVBlankTimeoutLogLimiter, Now, &Suppressed)) {
            WARNING(TEXT("Timeout pipe=%u timeout=%u sequence=%u suppressed=%u"),
                IntelGfxState.ActivePipeIndex,
                EffectiveTimeout,
                CurrentSequence,
                Suppressed);
        }
    }

    if (SequenceOut != NULL) {
        LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
        *SequenceOut = IntelGfxState.VBlankFrameSequence;
        UnlockMutex(&(IntelGfxState.PresentMutex));
    }

    return DF_RETURN_UNEXPECTED;
}

/************************************************************************/

void IntelGfxOnModeActivated(void) {
    BOOL InterruptEnabled = FALSE;

    LockMutex(&(IntelGfxState.PresentMutex), INFINITY);
    IntelGfxState.PresentFrameSequence = 0;
    IntelGfxState.VBlankFrameSequence = 0;
    IntelGfxState.VBlankInterruptCount = 0;
    IntelGfxState.VBlankPollCount = 0;
    IntelGfxState.LastVBlankScanline = MAX_U32;
    InterruptEnabled = IntelGfxTryEnableVBlankInterrupt(IntelGfxState.ActivePipeIndex);
    IntelGfxState.VBlankInterruptEnabled = InterruptEnabled;
    UnlockMutex(&(IntelGfxState.PresentMutex));

    if (IntelGfxVBlankTimeoutLogLimiter.Initialized) {
        RateLimiterReset(&IntelGfxVBlankTimeoutLogLimiter);
    }

    DEBUG(TEXT("VBlank path=%s pipe=%u"),
        InterruptEnabled ? TEXT("interrupt+poll") : TEXT("poll"),
        IntelGfxState.ActivePipeIndex);

    IntelGfxCursorOnModeActivated();
}

/************************************************************************/

UINT IntelGfxWaitVBlank(LPGFX_VBLANK_INFO Info) {
    UINT Result = DF_RETURN_GENERIC;
    U32 TimeoutMilliseconds = 0;
    U32 FrameSequence = 0;

    SAFE_USE(Info) { TimeoutMilliseconds = Info->TimeoutMilliseconds; }
    if (Info == NULL) {
        return DF_RETURN_GENERIC;
    }

    Result = IntelGfxWaitForNextVBlank(TimeoutMilliseconds, &FrameSequence);
    SAFE_USE(Info) { Info->FrameSequence = FrameSequence; }
    return Result;
}

/************************************************************************/
