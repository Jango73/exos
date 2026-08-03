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


    Desktop high-level text drawing

\************************************************************************/

#include "Desktop.h"

#include "core/KernelData.h"
#include "log/Log.h"

/***************************************************************************/

/**
 * @brief Draw one string in one graphics context using the current pen/brush colors.
 * @param TextInfo Text draw parameters.
 * @return TRUE on success.
 */
BOOL DesktopDrawText(LPGFX_TEXT_DRAW_INFO TextInfo) {
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_DRAW_INFO DrawInfo;
    UINT Result = 0;

    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(GFX_TEXT_DRAW_INFO)) return FALSE;
    if (TextInfo->Text == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)TextInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    DrawInfo = *TextInfo;
    DrawInfo.X = Context->Origin.X + DrawInfo.X;
    DrawInfo.Y = Context->Origin.Y + DrawInfo.Y;

    if (Context->Driver == NULL || Context->Driver->Command == NULL) return FALSE;

    Result = Context->Driver->Command(DF_GFX_TEXT_DRAW, (UINT)(LPVOID)&DrawInfo);

    return Result != 0 ? TRUE : FALSE;
}

/***************************************************************************/

/**
 * @brief Measure one string using one font face.
 * @param TextInfo Text measure parameters.
 * @return TRUE on success.
 */
BOOL DesktopMeasureText(LPGFX_TEXT_MEASURE_INFO TextInfo) {
    LPDRIVER Driver = NULL;

    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(GFX_TEXT_MEASURE_INFO)) return FALSE;
    if (TextInfo->Text == NULL) return FALSE;

    Driver = GetGraphicsDriver();
    if (Driver == NULL || Driver->Command == NULL) return FALSE;

    return Driver->Command(DF_GFX_TEXT_MEASURE, (UINT)(LPVOID)TextInfo) != 0 ? TRUE : FALSE;
}

/***************************************************************************/

BOOL DrawText(LPTEXT_DRAW_INFO TextInfo) {
    GFX_TEXT_DRAW_INFO GraphicsTextInfo;

    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(TEXT_DRAW_INFO)) return FALSE;

    GraphicsTextInfo.Header = TextInfo->Header;
    GraphicsTextInfo.Header.Size = sizeof(GFX_TEXT_DRAW_INFO);
    GraphicsTextInfo.GC = TextInfo->GC;
    GraphicsTextInfo.X = TextInfo->X;
    GraphicsTextInfo.Y = TextInfo->Y;
    GraphicsTextInfo.Text = TextInfo->Text;
    GraphicsTextInfo.Font = (const struct tag_FONT_FACE*)(LPVOID)TextInfo->Font;

    return DesktopDrawText(&GraphicsTextInfo);
}

/***************************************************************************/

BOOL MeasureText(LPTEXT_MEASURE_INFO TextInfo) {
    GFX_TEXT_MEASURE_INFO GraphicsTextInfo;

    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(TEXT_MEASURE_INFO)) return FALSE;

    GraphicsTextInfo.Header = TextInfo->Header;
    GraphicsTextInfo.Header.Size = sizeof(GFX_TEXT_MEASURE_INFO);
    GraphicsTextInfo.Text = TextInfo->Text;
    GraphicsTextInfo.Font = (const struct tag_FONT_FACE*)(LPVOID)TextInfo->Font;
    GraphicsTextInfo.Width = TextInfo->Width;
    GraphicsTextInfo.Height = TextInfo->Height;

    if (DesktopMeasureText(&GraphicsTextInfo) == FALSE) {
        return FALSE;
    }

    TextInfo->Width = GraphicsTextInfo.Width;
    TextInfo->Height = GraphicsTextInfo.Height;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve multi-line debug information from the active graphics driver.
 * @param Info Receives driver-specific debug lines.
 * @return TRUE on success.
 */
BOOL GetGraphicsDebugInfo(LPDRIVER_DEBUG_INFO Info) {
    LPDRIVER Driver = NULL;

    if (Info == NULL) return FALSE;
    if (Info->Header.Size < sizeof(DRIVER_DEBUG_INFO)) return FALSE;

    Driver = GetGraphicsDriver();
    if (Driver == NULL || Driver->Command == NULL) return FALSE;

    BOOL Result = Driver->Command(DF_DEBUG_INFO, (UINT)(LPVOID)Info) == DF_RETURN_SUCCESS ? TRUE : FALSE;
    return Result;
}

/***************************************************************************/

/**
 * @brief Retrieve multi-line debug information from the active mouse driver.
 * @param Info Receives driver-specific debug lines.
 * @return TRUE on success.
 */
BOOL GetMouseDebugInfo(LPDRIVER_DEBUG_INFO Info) {
    LPDRIVER Driver = NULL;

    if (Info == NULL) return FALSE;
    if (Info->Header.Size < sizeof(DRIVER_DEBUG_INFO)) return FALSE;

    Driver = GetMouseDriver();
    if (Driver == NULL || Driver->Command == NULL) return FALSE;

    BOOL Result = Driver->Command(DF_DEBUG_INFO, (UINT)(LPVOID)Info) == DF_RETURN_SUCCESS ? TRUE : FALSE;
    return Result;
}

/***************************************************************************/
