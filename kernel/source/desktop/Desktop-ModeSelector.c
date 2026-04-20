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


    Desktop mode selector

\************************************************************************/

#include "Desktop-ModeSelector.h"

#include "log/Log.h"

/***************************************************************************/

/**
 * @brief Compare two candidate graphics modes for deterministic selection.
 * @param Candidate Candidate mode.
 * @param Selected Already selected mode.
 * @return TRUE when candidate should replace selected.
 */
static BOOL IsBetterDesktopModeCandidate(LPGRAPHICS_MODE_INFO Candidate, LPGRAPHICS_MODE_INFO Selected) {
    U32 CandidateArea;
    U32 SelectedArea;

    if (DesktopIsValidGraphicsModeInfo(Candidate) == FALSE) return FALSE;
    if (DesktopIsValidGraphicsModeInfo(Selected) == FALSE) return TRUE;

    CandidateArea = Candidate->Width * Candidate->Height;
    SelectedArea = Selected->Width * Selected->Height;

    if (CandidateArea > SelectedArea) return TRUE;
    if (CandidateArea < SelectedArea) return FALSE;

    if (Candidate->BitsPerPixel > Selected->BitsPerPixel) return TRUE;
    if (Candidate->BitsPerPixel < Selected->BitsPerPixel) return FALSE;

    if (Candidate->Width > Selected->Width) return TRUE;
    if (Candidate->Width < Selected->Width) return FALSE;

    if (Candidate->Height > Selected->Height) return TRUE;
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Check whether mode info contains a usable graphics mode.
 * @param ModeInfo Mode descriptor to validate.
 * @return TRUE when width/height/depth are valid.
 */
BOOL DesktopIsValidGraphicsModeInfo(LPGRAPHICS_MODE_INFO ModeInfo) {
    if (ModeInfo == NULL) return FALSE;
    if (ModeInfo->Width == 0 || ModeInfo->Height == 0 || ModeInfo->BitsPerPixel == 0) return FALSE;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize a graphics mode descriptor with ABI header values.
 * @param ModeInfo Descriptor to initialize.
 * @param ModeIndex Requested mode index.
 * @param Width Requested width (0 means backend-defined).
 * @param Height Requested height (0 means backend-defined).
 * @param BitsPerPixel Requested depth (0 means backend-defined).
 */
void DesktopInitializeGraphicsModeInfo(LPGRAPHICS_MODE_INFO ModeInfo, U32 ModeIndex, U32 Width, U32 Height, U32 BitsPerPixel) {
    if (ModeInfo == NULL) return;

    ModeInfo->Header.Size = sizeof(GRAPHICS_MODE_INFO);
    ModeInfo->Header.Version = EXOS_ABI_VERSION;
    ModeInfo->Header.Flags = 0;
    ModeInfo->ModeIndex = ModeIndex;
    ModeInfo->Width = Width;
    ModeInfo->Height = Height;
    ModeInfo->BitsPerPixel = BitsPerPixel;
}

/***************************************************************************/

/**
 * @brief Select one desktop graphics mode using driver enumeration APIs.
 * @param GraphicsDriver Driver used for mode enumeration.
 * @param SelectedMode Output selected mode.
 * @return TRUE when a mode was selected from mode enumeration.
 */
BOOL DesktopSelectGraphicsMode(LPDRIVER GraphicsDriver, LPGRAPHICS_MODE_INFO SelectedMode) {
    GRAPHICS_MODE_INFO Candidate;
    UINT RawModeCount;
    U32 ModeCount;
    U32 MaxModesToProbe;
    U32 Index;
    UINT QueryResult;
    BOOL HasSelectedMode;

    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL || SelectedMode == NULL) {
        return FALSE;
    }

    RawModeCount = GraphicsDriver->Command(DF_GFX_GETMODECOUNT, 0);
    ModeCount = (RawModeCount < DF_RETURN_FIRST) ? (U32)RawModeCount : 0;
    MaxModesToProbe = (ModeCount > 0) ? ModeCount : 1;
    HasSelectedMode = FALSE;

    DEBUG(TEXT("Probe driver=%s raw_count=%u max_probe=%u"),
        GraphicsDriver->Product,
        RawModeCount,
        MaxModesToProbe);

    DesktopInitializeGraphicsModeInfo(SelectedMode, INFINITY, 0, 0, 0);

    for (Index = 0; Index < MaxModesToProbe; Index++) {
        DesktopInitializeGraphicsModeInfo(&Candidate, Index, 0, 0, 0);
        QueryResult = GraphicsDriver->Command(DF_GFX_GETMODEINFO, (UINT)&Candidate);
        if (QueryResult != DF_RETURN_SUCCESS) {
            DEBUG(TEXT("Mode index=%u query failed result=%u"),
                Index,
                QueryResult);
            continue;
        }

        if (DesktopIsValidGraphicsModeInfo(&Candidate) == FALSE) {
            DEBUG(TEXT("Mode index=%u invalid width=%u height=%u bpp=%u"),
                Index,
                Candidate.Width,
                Candidate.Height,
                Candidate.BitsPerPixel);
            continue;
        }

        DEBUG(TEXT("Mode index=%u candidate=%ux%ux%u"),
            Index,
            Candidate.Width,
            Candidate.Height,
            Candidate.BitsPerPixel);

        if (IsBetterDesktopModeCandidate(&Candidate, SelectedMode)) {
            *SelectedMode = Candidate;
            HasSelectedMode = TRUE;
            DEBUG(TEXT("Mode index=%u selected=%ux%ux%u"),
                Index,
                SelectedMode->Width,
                SelectedMode->Height,
                SelectedMode->BitsPerPixel);
        } else {
        }
    }

    return HasSelectedMode;
}
