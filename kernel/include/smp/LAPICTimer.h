
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


    Local APIC timer calibration

\************************************************************************/

#ifndef LAPIC_TIMER_H_INCLUDED
#define LAPIC_TIMER_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/
// Local APIC timer calibration configuration

typedef struct tag_LAPICTIMER_CONFIG {
    BOOL Calibrated;   // TRUE once calibration has completed
    U32 FrequencyHz;   // Calibrated timer frequency in Hertz
    U32 InitialCount;  // Initial count value used during calibration
    U32 DivideConfig;  // Divide configuration used during calibration
} LAPICTIMER_CONFIG, *LPLAPICTIMER_CONFIG;

/***************************************************************************/
// Function prototypes

/**
 * Calibrate the Local APIC timer against the PIT reference clock.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL CalibrateLAPICTimer(void);

/**
 * Retrieve the calibrated Local APIC timer frequency.
 * @return Frequency in Hertz, 0 if not calibrated.
 */
U32 GetLAPICTimerFrequency(void);

/**
 * Retrieve the Local APIC timer calibration configuration.
 * @return Pointer to the calibration configuration structure.
 */
LPLAPICTIMER_CONFIG GetLAPICTimerConfig(void);

/***************************************************************************/

#endif  // LAPIC_TIMER_H_INCLUDED
