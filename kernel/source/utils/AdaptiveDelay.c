
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


    Adaptive Delay System with Exponential Backoff

\************************************************************************/

#include "utils/AdaptiveDelay.h"

#ifdef __KERNEL__
#include "log/Log.h"
#endif

/************************************************************************/

/**
 * @brief Initializes an adaptive delay state with default values.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_Initialize(LPADAPTIVE_DELAY_STATE State) {
    // DEBUG("[AdaptiveDelay_Initialize] State = %x", State);

    SAFE_USE(State) {
        State->CurrentDelay = ADAPTIVE_DELAY_MIN_TICKS;
        State->AttemptCount = 0;
        State->MinDelay = ADAPTIVE_DELAY_MIN_TICKS;
        State->MaxDelay = ADAPTIVE_DELAY_MAX_TICKS;
        State->BackoffFactor = ADAPTIVE_DELAY_BACKOFF_FACTOR;
        State->MaxAttempts = ADAPTIVE_DELAY_MAX_ATTEMPTS;
        State->IsActive = FALSE;

        /*
        DEBUG(TEXT("Initialized with MinDelay=%u MaxDelay=%u MaxAttempts=%u"),
              State->MinDelay, State->MaxDelay, State->MaxAttempts);
              */
    }
}

/************************************************************************/

/**
 * @brief Resets an adaptive delay state to initial values.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_Reset(LPADAPTIVE_DELAY_STATE State) {
    SAFE_USE(State) {
        /*
        DEBUG(TEXT("Resetting state (was attempt %u, delay %u)"),
              State->AttemptCount, State->CurrentDelay);
              */

        State->CurrentDelay = State->MinDelay;
        State->AttemptCount = 0;
        State->IsActive = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Gets the next delay value and increments the attempt count.
 * @param State Pointer to the delay state structure.
 * @return Delay in ticks, or 0 if max attempts reached.
 */
U32 AdaptiveDelay_GetNextDelay(LPADAPTIVE_DELAY_STATE State) {
    SAFE_USE(State) {
        if (State->AttemptCount >= State->MaxAttempts) {
            DEBUG(TEXT("Max attempts (%u) reached"), State->MaxAttempts);
            return 0;
        }

        U32 DelayToReturn = State->CurrentDelay;
        State->AttemptCount++;
        State->IsActive = TRUE;

        // Calculate next delay with exponential backoff
        U32 NextDelay = State->CurrentDelay * State->BackoffFactor;
        if (NextDelay > State->MaxDelay) {
            NextDelay = State->MaxDelay;
        }
        State->CurrentDelay = NextDelay;

        /*
        DEBUG(TEXT("Attempt %u/%u, returning delay %u ticks, next will be %u"),
              State->AttemptCount, State->MaxAttempts, DelayToReturn, State->CurrentDelay);
              */

        return DelayToReturn;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Checks if more attempts should be made.
 * @param State Pointer to the delay state structure.
 * @return TRUE if more attempts are allowed, FALSE otherwise.
 */
BOOL AdaptiveDelay_ShouldContinue(LPADAPTIVE_DELAY_STATE State) {
    DEBUG(TEXT("State = %x"), State);

    SAFE_USE(State) {
        BOOL ShouldContinue = (State->AttemptCount < State->MaxAttempts);

        /*
        DEBUG(TEXT("Attempt %u/%u, continue=%s"),
              State->AttemptCount, State->MaxAttempts, ShouldContinue ? "YES" : "NO");
              */

        return ShouldContinue;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Called when an operation succeeds to reset the delay state.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_OnSuccess(LPADAPTIVE_DELAY_STATE State) {
    SAFE_USE(State) {
        // DEBUG(TEXT("Success after %u attempts, resetting"), State->AttemptCount);
        AdaptiveDelay_Reset(State);
    }
}

/************************************************************************/

/**
 * @brief Called when an operation fails to prepare for next attempt.
 * @param State Pointer to the delay state structure.
 */
void AdaptiveDelay_OnFailure(LPADAPTIVE_DELAY_STATE State) {
    SAFE_USE(State) {
        /*
        DEBUG(TEXT("Failure on attempt %u, current delay=%u next delay=%u"),
              State->AttemptCount, State->CurrentDelay / State->BackoffFactor, State->CurrentDelay);
              */

        // State is already updated by GetNextDelay, just log the failure
    }
}
