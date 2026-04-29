/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime Adaptive Delay

\************************************************************************/

#ifndef EXOS_ADAPTIVE_DELAY_H_INCLUDED
#define EXOS_ADAPTIVE_DELAY_H_INCLUDED

/************************************************************************/

#include "exos-runtime-main.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define ADAPTIVE_DELAY_MIN_TICKS 10
#define ADAPTIVE_DELAY_MAX_TICKS 1000
#define ADAPTIVE_DELAY_BACKOFF_FACTOR 2
#define ADAPTIVE_DELAY_MAX_ATTEMPTS 10

/************************************************************************/

typedef struct tag_ADAPTIVE_DELAY_STATE {
    U32 CurrentDelay;
    U32 AttemptCount;
    U32 MinDelay;
    U32 MaxDelay;
    U32 BackoffFactor;
    U32 MaxAttempts;
    BOOL IsActive;
} ADAPTIVE_DELAY_STATE, *LPADAPTIVE_DELAY_STATE;

/************************************************************************/

void AdaptiveDelay_Initialize(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_Reset(LPADAPTIVE_DELAY_STATE State);
U32 AdaptiveDelay_GetNextDelay(LPADAPTIVE_DELAY_STATE State);
BOOL AdaptiveDelay_ShouldContinue(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_OnSuccess(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_OnFailure(LPADAPTIVE_DELAY_STATE State);

/************************************************************************/

#pragma pack(pop)

/************************************************************************/

#endif
