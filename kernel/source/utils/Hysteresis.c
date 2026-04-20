
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


    Generic Hysteresis Module

\************************************************************************/

#include "utils/Hysteresis.h"
#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Initialize hysteresis context with thresholds
 *
 * @param This Pointer to the hysteresis context structure
 * @param LowThreshold Lower threshold value for state transitions
 * @param HighThreshold Upper threshold value for state transitions
 * @param InitialValue Initial value to set in the context
 *
 * @details This function initializes a hysteresis context with the specified
 * thresholds and initial value. The low threshold must be less than the high
 * threshold. The initial state is determined by comparing the initial value
 * with the high threshold.
 *
 * @note The context memory is zeroed before initialization
 * @warning This pointer must not be NULL
 * @warning LowThreshold must be less than HighThreshold
 */
void Hysteresis_Initialize(LPHYSTERESIS This, U32 LowThreshold, U32 HighThreshold, U32 InitialValue) {
    SAFE_USE(This) {
        if (LowThreshold >= HighThreshold) {
            ERROR(TEXT("Invalid thresholds: Low=%u >= High=%u"), LowThreshold, HighThreshold);
            return;
        }

        MemorySet(This, 0, sizeof(HYSTERESIS));

        This->LowThreshold = LowThreshold;
        This->HighThreshold = HighThreshold;
        This->CurrentValue = InitialValue;
        This->State = (InitialValue >= HighThreshold);
        This->TransitionPending = FALSE;

    } else {
        ERROR(TEXT("Object NULL"));
    }
}

/************************************************************************/

/**
 * @brief Update hysteresis with new value and check for state changes
 *
 * @param This Pointer to the hysteresis context structure
 * @param NewValue New value to update the hysteresis with
 *
 * @return TRUE if a state transition occurred, FALSE otherwise
 *
 * @details This function implements hysteresis logic by checking if the new
 * value crosses the configured thresholds. State transitions occur when:
 * - In low state: value >= HighThreshold (transition to high state)
 * - In high state: value < LowThreshold (transition to low state)
 * When a transition occurs, the TransitionPending flag is set.
 *
 * @note The function updates CurrentValue regardless of state changes
 * @warning This pointer must not be NULL
 */
BOOL Hysteresis_Update(LPHYSTERESIS This, U32 NewValue) {
    SAFE_USE(This) {
        This->CurrentValue = NewValue;

        BOOL StateChanged = FALSE;

        // Hysteresis logic
        if (!This->State) {
            // Currently in low state, check if we cross high threshold
            if (NewValue >= This->HighThreshold) {
                This->State = TRUE;
                This->TransitionPending = TRUE;
                StateChanged = TRUE;
            }
        } else {
            // Currently in high state, check if we drop below low threshold
            if (NewValue < This->LowThreshold) {
                This->State = FALSE;
                This->TransitionPending = TRUE;
                StateChanged = TRUE;
            }
        }

        if (!StateChanged) {
        }

        return StateChanged;
    }

    ERROR(TEXT("Object NULL"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Get current hysteresis state
 *
 * @param This Pointer to the hysteresis context structure
 *
 * @return Current state: TRUE for high state, FALSE for low state
 *
 * @details Returns the current state of the hysteresis without modifying
 * any internal values. The state represents whether the system is currently
 * above or below the hysteresis thresholds.
 *
 * @warning This pointer must not be NULL
 * @warning Returns FALSE if This is NULL (error condition)
 */
BOOL Hysteresis_GetState(LPHYSTERESIS This) {
    SAFE_USE(This) {
        return This->State;
    }

    ERROR(TEXT("Object NULL"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Check if a transition event is pending
 *
 * @param This Pointer to the hysteresis context structure
 *
 * @return TRUE if a transition is pending, FALSE otherwise
 *
 * @details Returns the status of the transition pending flag, which is set
 * when a state change occurs during Hysteresis_Update(). This flag allows
 * callers to detect and handle state transitions appropriately.
 *
 * @note The flag remains set until explicitly cleared with Hysteresis_ClearTransition()
 * @warning This pointer must not be NULL
 * @warning Returns FALSE if This is NULL (error condition)
 */
BOOL Hysteresis_IsTransitionPending(LPHYSTERESIS This) {
    SAFE_USE(This) {
        return This->TransitionPending;
    }

    ERROR(TEXT("Object NULL"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Clear the transition pending flag
 *
 * @param This Pointer to the hysteresis context structure
 *
 * @details Clears the transition pending flag, indicating that the caller
 * has acknowledged and processed the state transition. This function should
 * be called after handling a detected state change.
 *
 * @note This function only affects the TransitionPending flag
 * @warning This pointer must not be NULL
 */
void Hysteresis_ClearTransition(LPHYSTERESIS This) {
    SAFE_USE(This) {
        if (This->TransitionPending) {
        }
        This->TransitionPending = FALSE;
        return;
    }

    ERROR(TEXT("Object NULL"));
}

/************************************************************************/

/**
 * @brief Get current value
 *
 * @param This Pointer to the hysteresis context structure
 *
 * @return Current value stored in the hysteresis context
 *
 * @details Returns the most recently updated value in the hysteresis context.
 * This is the value that was last passed to Hysteresis_Update() or set during
 * initialization/reset.
 *
 * @warning This pointer must not be NULL
 * @warning Returns 0 if This is NULL (error condition)
 */
U32 Hysteresis_GetValue(LPHYSTERESIS This) {
    SAFE_USE(This) {
        return This->CurrentValue;
    }

    ERROR(TEXT("Object NULL"));
    return 0;
}

/************************************************************************/

/**
 * @brief Reset hysteresis context
 *
 * @param This Pointer to the hysteresis context structure
 * @param NewValue New value to reset the context with
 *
 * @details Resets the hysteresis context with a new value, recalculating
 * the state based on the high threshold and clearing any pending transitions.
 * The thresholds remain unchanged from the original initialization.
 *
 * @note The state is recalculated as (NewValue >= HighThreshold)
 * @note TransitionPending flag is cleared during reset
 * @warning This pointer must not be NULL
 */
void Hysteresis_Reset(LPHYSTERESIS This, U32 NewValue) {
    SAFE_USE(This) {
        This->CurrentValue = NewValue;
        This->State = (NewValue >= This->HighThreshold);
        This->TransitionPending = FALSE;

        return;
    }

    ERROR(TEXT("Object NULL"));
}
