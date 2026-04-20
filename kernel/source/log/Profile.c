
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


    Profiling helpers

\************************************************************************/

#include "log/Profile.h"
#include "system/Clock.h"
#include "text/CoreString.h"
#include "log/Log.h"
#include "system/System.h"

#if PROFILING

/************************************************************************/
// Time source: PIT counter read (channel 0, latched), for sub-ms deltas.
// We combine PIT ticks with coarse GetSystemTime() to handle wrap.
static inline UINT ProfileGetTicks(void)
{
    U32 Flags;
    U8 Low;
    U8 High;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(0x43, 0x00);           // Latch channel 0 count
    Low = InPortByte(0x40);            // Read low byte
    High = InPortByte(0x40);           // Read high byte

    RestoreFlags(&Flags);

    UINT Latched = ((UINT)High << 8) | (UINT)Low;

    // Combine with coarse system time to avoid wrap confusion
    // (PIT counts down from DIVISOR to 0).
    UINT CoarseMs = GetSystemTime();
    return (CoarseMs << 16) | Latched;
}

/************************************************************************/

#define PROFILE_MAX_STATS 128
#define PIT_FREQUENCY 1193180u
#define PIT_DIVISOR 11932u

static PROFILE_STATS ProfileStats[PROFILE_MAX_STATS];
static UINT ProfileStatsCount = 0;
static UINT ProfileSamplesWritten = 0;
static UINT ProfileSamplesDropped = 0;

/************************************************************************/

static UINT ProfileReadPITCount(void)
{
    U32 Flags;
    U8 Low;
    U8 High;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(0x43, 0x00);           // Latch channel 0 count
    Low = InPortByte(0x40);            // Read low byte
    High = InPortByte(0x40);           // Read high byte

    RestoreFlags(&Flags);

    return ((UINT)High << 8) | (UINT)Low;
}

/************************************************************************/

static LPPROFILE_STATS ProfileFindOrCreate(LPCSTR Name)
{
    for (UINT Index = 0; Index < ProfileStatsCount; ++Index)
    {
        if (ProfileStats[Index].Name == Name)
        {
            return &ProfileStats[Index];
        }
    }

    if (ProfileStatsCount >= PROFILE_MAX_STATS)
    {
        ProfileSamplesDropped++;
        return NULL;
    }

    ProfileStats[ProfileStatsCount].Name = Name;
    ProfileStats[ProfileStatsCount].CallCount = 0;
    ProfileStats[ProfileStatsCount].TimedCallCount = 0;
    ProfileStats[ProfileStatsCount].LastTicks = 0;
    ProfileStats[ProfileStatsCount].TotalTicks = 0;
    ProfileStats[ProfileStatsCount].MaxTicks = 0;

    ProfileStatsCount++;
    return &ProfileStats[ProfileStatsCount - 1];
}

/************************************************************************/

static void ProfileRecordSample(LPCSTR Name, UINT DurationTicks)
{
    if (Name == NULL)
    {
        return;
    }

    LPPROFILE_STATS Entry = ProfileFindOrCreate(Name);
    if (Entry == NULL)
    {
        ProfileSamplesDropped++;
        return;
    }

    Entry->TimedCallCount++;
    Entry->LastTicks = DurationTicks;
    Entry->TotalTicks += DurationTicks;
    if (DurationTicks > Entry->MaxTicks)
    {
        Entry->MaxTicks = DurationTicks;
    }

    ProfileSamplesWritten++;
}

/************************************************************************/

/**
 * @brief Compute the duration of one profiling scope.
 * @param Scope Profiling scope to inspect.
 * @return Duration in microseconds.
 */
static UINT ProfileComputeScopeDuration(LPPROFILE_SCOPE Scope)
{
    UINT EndMillis;
    UINT EndCount;
    UINT BaseMillis;
    UINT BaseMicros;
    UINT StartOffsetMicros;
    UINT EndOffsetMicros;
    INT OffsetDelta;
    INT DurationMicrosSigned;

    if (Scope == NULL)
    {
        return 0;
    }

    EndMillis = GetSystemTime();
    EndCount = ProfileReadPITCount();

    BaseMillis = (EndMillis >= Scope->StartMillis) ? (EndMillis - Scope->StartMillis) : 0;
    BaseMicros = BaseMillis * 1000;

    StartOffsetMicros = ((PIT_DIVISOR - Scope->StartCount) * 1000000) / PIT_FREQUENCY;
    EndOffsetMicros = ((PIT_DIVISOR - EndCount) * 1000000) / PIT_FREQUENCY;

    OffsetDelta = (INT)EndOffsetMicros - (INT)StartOffsetMicros;
    DurationMicrosSigned = (INT)BaseMicros + OffsetDelta;

    return (DurationMicrosSigned >= 0) ? (UINT)DurationMicrosSigned : 0;
}

/************************************************************************/

/**
 * @brief Increment the lightweight call counter for one profile entry.
 *
 * @param Name Entry name bound to a static call site.
 */
void ProfileCountCall(LPCSTR Name)
{
    LPPROFILE_STATS Entry;

    if (Name == NULL)
    {
        return;
    }

    Entry = ProfileFindOrCreate(Name);
    if (Entry == NULL)
    {
        ProfileSamplesDropped++;
        return;
    }

    Entry->CallCount++;
}

/************************************************************************/

/**
 * @brief Record a measured profiling duration.
 *
 * @param Name Entry name bound to a static call site.
 * @param DurationMicros Duration in microseconds.
 */
void ProfileRecordDuration(LPCSTR Name, UINT DurationMicros)
{
    ProfileCountCall(Name);
    ProfileRecordSample(Name, DurationMicros);
}

/************************************************************************/

/**
 * @brief Start a profiling scope.
 *
 * Initializes the scope with the provided name and records the start tick
 * placeholder. Timing is refined in later steps.
 *
 * @param Scope Profiling scope structure to initialize.
 * @param Name  Scope name (literal or static string).
 */
void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    if (Scope == NULL)
    {
        return;
    }

    ProfileCountCall(Name);
    Scope->Name = Name;
    Scope->StartMillis = GetSystemTime();
    Scope->StartCount = ProfileReadPITCount();
    Scope->State = PROFILE_SCOPE_STATE_ACTIVE;
}

/************************************************************************/

/**
 * @brief Stop a profiling scope.
 *
 * Records completion of the scope. Duration accounting is added in later
 * steps alongside storage and timing sources.
 *
 * @param Scope Profiling scope to finalize.
 */
void ProfileStop(LPPROFILE_SCOPE Scope)
{
    (void)ProfileStopDuration(Scope);
}

/************************************************************************/

/**
 * @brief Stop a profiling scope and return the recorded duration.
 * @param Scope Profiling scope to finalize.
 * @return Duration in microseconds.
 */
UINT ProfileStopDuration(LPPROFILE_SCOPE Scope)
{
    UINT DurationMicros;

    if (Scope == NULL)
    {
        return 0;
    }

    Scope->State = PROFILE_SCOPE_STATE_INACTIVE;
    DurationMicros = ProfileComputeScopeDuration(Scope);

    ProfileRecordSample(Scope->Name, DurationMicros);
    return DurationMicros;
}

/************************************************************************/

/**
 * @brief Capture a bounded snapshot of profiling counters and timings.
 *
 * @param Info Snapshot descriptor and destination buffer.
 * @return UINT Number of copied entries.
 */
UINT ProfileGetStats(LPPROFILE_QUERY_INFO Info)
{
    UINT EntryCount;

    if (Info == NULL)
    {
        return 0;
    }

    EntryCount = ProfileStatsCount;
    if (EntryCount > Info->Capacity)
    {
        EntryCount = Info->Capacity;
    }

    Info->EntryCount = EntryCount;
    Info->TotalEntryCount = ProfileStatsCount;
    Info->SampleCount = ProfileSamplesWritten;
    Info->DroppedCount = ProfileSamplesDropped;

    for (UINT Index = 0; Index < EntryCount; ++Index)
    {
        MemorySet(&Info->Entries[Index], 0, sizeof(PROFILE_ENTRY_INFO));
        StringCopyLimit(Info->Entries[Index].Name, ProfileStats[Index].Name != NULL ? ProfileStats[Index].Name : TEXT(""), PROFILE_NAME_LENGTH - 1);
        Info->Entries[Index].CallCount = ProfileStats[Index].CallCount;
        Info->Entries[Index].TimedCallCount = ProfileStats[Index].TimedCallCount;
        Info->Entries[Index].LastTicks = ProfileStats[Index].LastTicks;
        Info->Entries[Index].TotalTicks = ProfileStats[Index].TotalTicks;
        Info->Entries[Index].MaxTicks = ProfileStats[Index].MaxTicks;
    }

    if ((Info->Flags & PROFILE_QUERY_FLAG_RESET) != 0)
    {
        ProfileStatsCount = 0;
        ProfileSamplesWritten = 0;
        ProfileSamplesDropped = 0;
        MemorySet(ProfileStats, 0, sizeof(ProfileStats));
    }

    return EntryCount;
}

/************************************************************************/

#else

void ProfileCountCall(LPCSTR Name)
{
    UNUSED(Name);
}

/************************************************************************/

void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    UNUSED(Scope);
    UNUSED(Name);
}

/************************************************************************/

void ProfileStop(LPPROFILE_SCOPE Scope)
{
    UNUSED(Scope);
}

/************************************************************************/

UINT ProfileStopDuration(LPPROFILE_SCOPE Scope)
{
    UNUSED(Scope);
    return 0;
}

/************************************************************************/

void ProfileRecordDuration(LPCSTR Name, UINT DurationMicros)
{
    UNUSED(Name);
    UNUSED(DurationMicros);
}

/************************************************************************/

UINT ProfileGetStats(LPPROFILE_QUERY_INFO Info)
{
    if (Info != NULL)
    {
        Info->EntryCount = 0;
        Info->TotalEntryCount = 0;
        Info->SampleCount = 0;
        Info->DroppedCount = 0;
    }

    return 0;
}

/************************************************************************/

#endif
