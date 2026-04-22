/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime Adaptive Delay

\************************************************************************/

#include "../include/exos-adaptive-delay.h"

/************************************************************************/

/**
 * @brief Initializes an adaptive delay state with default values.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_Initialize(LPADAPTIVE_DELAY_STATE State) {
    if (State == NULL) {
        return;
    }

    State->CurrentDelay = ADAPTIVE_DELAY_MIN_TICKS;
    State->AttemptCount = 0;
    State->MinDelay = ADAPTIVE_DELAY_MIN_TICKS;
    State->MaxDelay = ADAPTIVE_DELAY_MAX_TICKS;
    State->BackoffFactor = ADAPTIVE_DELAY_BACKOFF_FACTOR;
    State->MaxAttempts = ADAPTIVE_DELAY_MAX_ATTEMPTS;
    State->IsActive = FALSE;
}

/************************************************************************/

/**
 * @brief Resets an adaptive delay state to initial values.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_Reset(LPADAPTIVE_DELAY_STATE State) {
    if (State == NULL) {
        return;
    }

    State->CurrentDelay = State->MinDelay;
    State->AttemptCount = 0;
    State->IsActive = FALSE;
}

/************************************************************************/

/**
 * @brief Gets the next delay value and increments the attempt count.
 * @param State Pointer to the delay state structure.
 * @return Delay in ticks, or 0 when the maximum attempt count is reached.
 */
U32 AdaptiveDelay_GetNextDelay(LPADAPTIVE_DELAY_STATE State) {
    U32 DelayToReturn;
    U32 NextDelay;

    if (State == NULL) {
        return 0;
    }

    if (State->AttemptCount >= State->MaxAttempts) {
        return 0;
    }

    DelayToReturn = State->CurrentDelay;
    State->AttemptCount++;
    State->IsActive = TRUE;

    NextDelay = State->CurrentDelay * State->BackoffFactor;
    if (NextDelay > State->MaxDelay) {
        NextDelay = State->MaxDelay;
    }
    State->CurrentDelay = NextDelay;

    return DelayToReturn;
}

/************************************************************************/

/**
 * @brief Checks if more attempts should be made.
 * @param State Pointer to the delay state structure.
 * @return TRUE if more attempts are allowed, FALSE otherwise.
 */
BOOL AdaptiveDelay_ShouldContinue(LPADAPTIVE_DELAY_STATE State) {
    if (State == NULL) {
        return FALSE;
    }

    return State->AttemptCount < State->MaxAttempts;
}

/************************************************************************/

/**
 * @brief Called when an operation succeeds to reset the delay state.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_OnSuccess(LPADAPTIVE_DELAY_STATE State) {
    if (State == NULL) {
        return;
    }

    AdaptiveDelay_Reset(State);
}

/************************************************************************/

/**
 * @brief Called when an operation fails.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_OnFailure(LPADAPTIVE_DELAY_STATE State) {
    UNUSED(State);
}
