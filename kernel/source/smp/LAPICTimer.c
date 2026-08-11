
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

    Calibrates the Local APIC timer frequency against the PIT channel 0
    reference clock during early boot, before interrupts are enabled.

\************************************************************************/

#include "smp/LAPICTimer.h"

#include "Arch.h"
#include "drivers/interrupts/LocalAPIC.h"
#include "log/Log.h"
#include "system/System.h"

/***************************************************************************/

#define LAPICTIMER_PIT_COMMAND 0x43   // PIT command register
#define LAPICTIMER_PIT_CHANNEL0 0x40  // PIT channel 0 data register

#define LAPICTIMER_PIT_DIVISOR 11932             // 1,193,180 Hz / 10 ms reference window
#define LAPICTIMER_REFERENCE_MICROS 10000        // PIT one-shot reference window
#define LAPICTIMER_CALIBRATION_COUNT 0xFFFFFFFF  // Maximum timer initial count
#define LAPICTIMER_POLL_LIMIT 2000000            // Loop limit fallback for the PIT wait

/***************************************************************************/

static LAPICTIMER_CONFIG DATA_SECTION G_LAPICTimerConfig = { 0 };

/***************************************************************************/

/**
 * @brief Retrieve the Local APIC timer calibration configuration.
 * @return Pointer to the calibration configuration structure.
 */
LPLAPICTIMER_CONFIG GetLAPICTimerConfig(void) {
    return &G_LAPICTimerConfig;
}

/***************************************************************************/

/**
 * @brief Retrieve the calibrated Local APIC timer frequency.
 * @return Frequency in Hertz, 0 if not calibrated.
 */
U32 GetLAPICTimerFrequency(void) {
    return G_LAPICTimerConfig.FrequencyHz;
}

/***************************************************************************/

/**
 * @brief Latch and read the current PIT channel 0 count value.
 * @return Current 16-bit PIT count value.
 */
static U16 ReadPitCounter(void) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(LAPICTIMER_PIT_COMMAND, 0x00);  // Latch channel 0 count
    U8 Low = (U8)InPortByte(LAPICTIMER_PIT_CHANNEL0);
    U8 High = (U8)InPortByte(LAPICTIMER_PIT_CHANNEL0);

    RestoreFlags(&Flags);

    return (U16)(((U16)High << 8) | (U16)Low);
}

/***************************************************************************/

/**
 * @brief Calibrate the Local APIC timer against the PIT reference clock.
 *
 * Programs the PIT channel 0 in one-shot mode for a 10 ms reference window,
 * starts the Local APIC timer at its maximum count, waits for the window to
 * elapse and derives the timer frequency from the decremented count. The PIT
 * is restored to its periodic clock configuration afterwards.
 *
 * @return TRUE on success, FALSE otherwise.
 */
BOOL CalibrateLAPICTimer(void) {
    LPLOCAL_APIC_CONFIG LocalApicConfig = GetLocalAPICConfig();

    if (LocalApicConfig == NULL || LocalApicConfig->Present == FALSE) {
        DEBUG(TEXT("[CalibrateLAPICTimer] Local APIC not available, calibration skipped"));
        return FALSE;
    }

    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    // Program PIT channel 0 in one-shot mode (mode 0) for the reference window
    OutPortByte(LAPICTIMER_PIT_COMMAND, 0x30);
    OutPortByte(LAPICTIMER_PIT_CHANNEL0, (U8)(LAPICTIMER_PIT_DIVISOR & 0xFF));
    OutPortByte(LAPICTIMER_PIT_CHANNEL0, (U8)(LAPICTIMER_PIT_DIVISOR >> 8));

    // Start the Local APIC timer at its maximum count
    WriteLocalAPICRegister(LOCAL_APIC_TIMER_DCR, LOCAL_APIC_TIMER_DIVIDE_BY_16);
    WriteLocalAPICRegister(LOCAL_APIC_TIMER_ICR, LAPICTIMER_CALIBRATION_COUNT);

    // Wait for the PIT reference window to elapse
    U32 Polls = 0;

    while (ReadPitCounter() != 0) {
        if (++Polls >= LAPICTIMER_POLL_LIMIT) {
            WriteLocalAPICRegister(LOCAL_APIC_TIMER_ICR, 0);
            WARNING(TEXT("[CalibrateLAPICTimer] PIT reference window never elapsed, calibration failed"));
            RestoreFlags(&Flags);
            return FALSE;
        }
    }

    U32 CurrentCount = ReadLocalAPICRegister(LOCAL_APIC_TIMER_CCR);
    U32 CountsDecremented = LAPICTIMER_CALIBRATION_COUNT - CurrentCount;

    // Stop the Local APIC timer
    WriteLocalAPICRegister(LOCAL_APIC_TIMER_ICR, 0);

    // Restore PIT channel 0 to its periodic clock configuration (mode 3)
    OutPortByte(LAPICTIMER_PIT_COMMAND, 0x36);
    OutPortByte(LAPICTIMER_PIT_CHANNEL0, (U8)(LAPICTIMER_PIT_DIVISOR & 0xFF));
    OutPortByte(LAPICTIMER_PIT_CHANNEL0, (U8)(LAPICTIMER_PIT_DIVISOR >> 8));

    RestoreFlags(&Flags);

    if (CountsDecremented == 0) {
        WARNING(TEXT("[CalibrateLAPICTimer] Local APIC timer did not advance, calibration failed"));
        return FALSE;
    }

    U64 Product = U64_MultiplyU32(CountsDecremented, (U32)(1000000 / LAPICTIMER_REFERENCE_MICROS));
    U32 FrequencyHz = U64_ToU32_Clip(Product);

    G_LAPICTimerConfig.Calibrated = TRUE;
    G_LAPICTimerConfig.FrequencyHz = FrequencyHz;
    G_LAPICTimerConfig.InitialCount = LAPICTIMER_CALIBRATION_COUNT;
    G_LAPICTimerConfig.DivideConfig = LOCAL_APIC_TIMER_DIVIDE_BY_16;

    DEBUG(
        TEXT("[CalibrateLAPICTimer] Counts=%u over %u ms, frequency=%u Hz (divide config %x)"),
        CountsDecremented,
        LAPICTIMER_REFERENCE_MICROS / 1000,
        G_LAPICTimerConfig.FrequencyHz,
        G_LAPICTimerConfig.DivideConfig);

    return TRUE;
}

/***************************************************************************/
