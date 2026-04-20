
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    Clock manager

\************************************************************************/

#include "system/Clock.h"

#include "Arch.h"
#include "drivers/interrupts/InterruptController.h"
#include "core/Kernel.h"
#include "core/KernelData.h"
#include "log/Log.h"
#include "process/Schedule.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "text/Text.h"

/************************************************************************/

#define CLOCK_VER_MAJOR 1
#define CLOCK_VER_MINOR 0

static UINT ClockDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION ClockDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_CLOCK,
    .VersionMajor = CLOCK_VER_MAJOR,
    .VersionMinor = CLOCK_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "Clock",
    .Alias = "clock",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = ClockDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the clock driver descriptor.
 * @return Pointer to the clock driver.
 */
LPDRIVER ClockGetDriver(void) {
    return &ClockDriver;
}

/************************************************************************/
// Timer resolution

#define DIVISOR 11932
#define MILLIS 10
#define SCHEDULING_PERIOD_MILLIS 10

/************************************************************************/

typedef struct tag_CLOCK_STATE {
    UINT SystemUpTime;
    UINT SchedulerTime;
    UINT PreInterruptSystemTime;
    BOOL SystemTimeOperational;
    DATETIME CurrentTime;
    U8 DaysInMonth[12];

#if SCHEDULING_DEBUG_OUTPUT == 1
    UINT DebugLogCount;
#endif
} CLOCK_STATE, *LPCLOCK_STATE;

/************************************************************************/

static CLOCK_STATE DATA_SECTION ClockState = {
    .SystemUpTime = 0,
    .SchedulerTime = 0,
    .PreInterruptSystemTime = 0,
    .SystemTimeOperational = FALSE,
    .CurrentTime = {0},
    .DaysInMonth = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},

#if SCHEDULING_DEBUG_OUTPUT == 1
    .DebugLogCount = 0,
#endif
};

/************************************************************************/

static BOOL IsLeapYear(U32 Year) { return (Year % 400 == 0) || ((Year % 4 == 0) && (Year % 100 != 0)); }

/************************************************************************/

static U32 BCDToInteger(U32 Value) { return ((Value >> 4) * 10) + (Value & 0x0F); }

/************************************************************************/

/**
 * @brief Produce a monotonic timestamp before the first clock interrupt.
 * @return Synthetic pre-interrupt system time in milliseconds.
 */
static UINT GetPreInterruptSystemTime(void) {
    if (ClockState.PreInterruptSystemTime <= MAX_UINT - MILLIS) {
        ClockState.PreInterruptSystemTime += MILLIS;
        return ClockState.PreInterruptSystemTime;
    }

    return MAX_UINT;
}

/**
 * @brief Read a byte from the CMOS at the given address.
 * @param Address CMOS register address.
 * @return Value read from CMOS.
 */
static U32 ReadCMOS(U32 Address) {
    OutPortByte(CMOS_COMMAND, Address);
    return InPortByte(CMOS_DATA);
}

/************************************************************************/

/**
 * @brief Program PIT channel 0 for the kernel clock period.
 */
static void ProgramPITChannel0(void) {
    OutPortByte(CLOCK_COMMAND, 0x36);
    OutPortByte(CLOCK_DATA, (U8)(DIVISOR >> 0));
    OutPortByte(CLOCK_DATA, (U8)(DIVISOR >> 8));
}

/************************************************************************/

static void InitializeLocalTime(void) {
    ClockState.CurrentTime.Year = 2000 + BCDToInteger(ReadCMOS(CMOS_YEAR));
    ClockState.CurrentTime.Month = BCDToInteger(ReadCMOS(CMOS_MONTH));
    ClockState.CurrentTime.Day = BCDToInteger(ReadCMOS(CMOS_DAY_OF_MONTH));
    ClockState.CurrentTime.Hour = BCDToInteger(ReadCMOS(CMOS_HOUR));
    ClockState.CurrentTime.Minute = BCDToInteger(ReadCMOS(CMOS_MINUTE));
    ClockState.CurrentTime.Second = BCDToInteger(ReadCMOS(CMOS_SECOND));
    ClockState.CurrentTime.Milli = 0;
}

/************************************************************************/

/**
 * @brief Initialize the system clock and enable timer interrupts.
 */
void InitializeClock(void) {
    // The 8254 Timer Chip receives 1,193,180 signals from
    // the system, so to increment a 10 millisecond counter,
    // our interrupt handler must be called every 11,932 signal
    // 1,193,180 / 11,932 = 99.99832384

    U32 Flags;

    SaveFlags(&Flags);

    ProgramPITChannel0();
    ClockState.PreInterruptSystemTime = 0;

    RestoreFlags(&Flags);

    EnableInterrupt(0);
    InitializeLocalTime();
    SetKernelBootTime(&ClockState.CurrentTime);
}

/************************************************************************/

void ManageLocalTime(void) {
    ClockState.CurrentTime.Milli -= 1000;
    ClockState.CurrentTime.Second++;

    if (ClockState.CurrentTime.Second >= 60) {
        ClockState.CurrentTime.Second = 0;
        ClockState.CurrentTime.Minute++;

        if (ClockState.CurrentTime.Minute >= 60) {
            ClockState.CurrentTime.Minute = 0;
            ClockState.CurrentTime.Hour++;

            if (ClockState.CurrentTime.Hour >= 24) {
                ClockState.CurrentTime.Hour = 0;
                ClockState.CurrentTime.Day++;
                U32 DaysInCurrentMonth = ClockState.DaysInMonth[ClockState.CurrentTime.Month - 1];

                if (ClockState.CurrentTime.Month == 2 && IsLeapYear(ClockState.CurrentTime.Year)) {
                    DaysInCurrentMonth++;
                }

                if (ClockState.CurrentTime.Day > DaysInCurrentMonth) {
                    ClockState.CurrentTime.Day = 1;
                    ClockState.CurrentTime.Month++;
                    if (ClockState.CurrentTime.Month > 12) {
                        ClockState.CurrentTime.Month = 1;
                        ClockState.CurrentTime.Year++;
                    }
                }
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Increment the internal millisecond counter.
 */
void ClockHandler(void) {
#if SCHEDULING_DEBUG_OUTPUT == 1
    ClockState.DebugLogCount++;
    if (ClockState.DebugLogCount > 20000) {
        DEBUG(TEXT("Too much flooding, halting system."));
        DO_THE_SLEEPING_BEAUTY;
    }
#endif

    if (ClockState.SystemUpTime == 0 &&
        ClockState.SystemTimeOperational != FALSE &&
        ClockState.PreInterruptSystemTime != 0) {
        ClockState.SystemUpTime = ClockState.PreInterruptSystemTime;
    }

    ClockState.SystemUpTime += MILLIS;
    ClockState.SchedulerTime += MILLIS;
    ClockState.CurrentTime.Milli += MILLIS;

    while (ClockState.CurrentTime.Milli >= 1000) {
        ManageLocalTime();
    }

    UINT MinimumQuantum = GetMinimumQuantum();

    if (ClockState.SchedulerTime >= (MinimumQuantum + SCHEDULING_PERIOD_MILLIS)) {
        ClockState.SchedulerTime = 0;
        Scheduler();
    }

    /*
    if (SystemUpTime % 1000 == 0) {
        DEBUG(TEXT("[ClockHandler] Time = %d"), SystemUpTime);
    }
    */
}

/************************************************************************/

/**
 * @brief Retrieve the current system time in milliseconds.
 * @return Number of milliseconds since startup.
 */
UINT GetSystemTime(void) {
    if ((ClockDriver.Flags & DRIVER_FLAG_READY) != 0 &&
        ClockState.SystemTimeOperational != FALSE &&
        ClockState.SystemUpTime == 0) {
        return GetPreInterruptSystemTime();
    }

    return ClockState.SystemUpTime;
}

/************************************************************************/

/**
 * @brief Mark time-based sleeping as operational after first interrupt enable.
 */
void MarkSystemTimeOperational(void) {
    ClockState.SystemTimeOperational = TRUE;
}

/************************************************************************/

/**
 * @brief Check whether system time can be relied upon for delays.
 *
 * @return TRUE once first interrupt enable has been executed.
 */
BOOL IsSystemTimeOperational(void) {
    return ClockState.SystemTimeOperational;
}

/************************************************************************/

/**
 * @brief Check whether one operation timeout has been reached.
 *
 * This helper is safe for early-boot polling paths where GetSystemTime()
 * can remain constant until interrupts are enabled. It always keeps a loop
 * limit fallback in addition to elapsed-time validation.
 *
 * @param StartTime Start value from GetSystemTime().
 * @param LoopCount Current polling loop index.
 * @param LoopLimit Maximum polling loops before timeout.
 * @param TimeoutMilliseconds Timeout window in milliseconds.
 * @return TRUE when timeout is reached, FALSE otherwise.
 */
BOOL HasOperationTimedOut(UINT StartTime, UINT LoopCount, UINT LoopLimit, UINT TimeoutMilliseconds) {
    if (LoopCount >= LoopLimit) {
        return TRUE;
    }

    UINT CurrentTime = GetSystemTime();
    if (CurrentTime == StartTime) {
        return FALSE;
    }

    return ((UINT)(CurrentTime - StartTime) >= TimeoutMilliseconds);
}

/************************************************************************/

/**
 * @brief Convert milliseconds to HH:MM:SS text representation.
 * @param MilliSeconds Time in milliseconds.
 * @param Text Destination buffer for formatted string.
 */
void MilliSecondsToHMS(UINT MilliSeconds, LPSTR Text) {
    UINT Seconds = MilliSeconds / 1000;
    UINT H = (Seconds / 3600);
    UINT M = (Seconds / 60) % 60;
    UINT S = (Seconds % 60);
    STR Temp[16];

    // sprintf(Text, "%02u:%02u:%02u", h, m, s);

    Text[0] = STR_NULL;

    if (H < 10) StringConcat(Text, Text_0);
    U32ToString((U32)H, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_Colon);
    if (M < 10) StringConcat(Text, Text_0);
    U32ToString((U32)M, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_Colon);
    if (S < 10) StringConcat(Text, Text_0);
    U32ToString((U32)S, Temp);
    StringConcat(Text, Temp);
}

/************************************************************************/

/**
 * @brief Retrieve the recorded boot local time.
 * @param Time Destination structure for the boot time.
 * @return TRUE on success.
 */
BOOL GetBootLocalTime(LPDATETIME Time) {
    return GetKernelBootTime(Time);
}

/************************************************************************/

/*
static void WriteCMOS(U32 Address, U32 Value) {
    OutPortByte(CMOS_COMMAND, Address);
    OutPortByte(CMOS_DATA, Value);
}
*/

/************************************************************************/

/**
 * @brief Retrieve current time from CMOS into a DATETIME structure.
 * @param Time Destination structure for the current time.
 * @return TRUE on success.
 */
BOOL GetLocalTime(LPDATETIME Time) {
    if (Time == NULL) return FALSE;
    if (ClockState.CurrentTime.Year == 0) {
        InitializeLocalTime();
    }
    *Time = ClockState.CurrentTime;
    return TRUE;
}

/************************************************************************/

BOOL SetLocalTime(LPDATETIME Time) {
    if (Time == NULL) return FALSE;
    ClockState.CurrentTime = *Time;
    return TRUE;
}

/************************************************************************/

void RTCHandler(void) { DEBUG(TEXT("[RTCHandler]")); }

/************************************************************************/

void PIC2Handler(void) { DEBUG(TEXT("[PIC2Handler]")); }

/************************************************************************/

void FPUHandler(void) { DEBUG(TEXT("[FPUHandler]")); }

/************************************************************************/

/**
 * @brief Driver command handler for the clock subsystem.
 *
 * DF_LOAD initializes the clock once; DF_UNLOAD only clears readiness.
 */
static UINT ClockDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((ClockDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeClock();
            ClockDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((ClockDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ClockDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(CLOCK_VER_MAJOR, CLOCK_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
