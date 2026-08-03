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


    On-screen debug information component

\************************************************************************/

#include "ui/OnScreenDebugInfo.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

/***************************************************************************/

#define ON_SCREEN_DEBUG_INFO_LINE_COUNT 8
#define ON_SCREEN_DEBUG_INFO_PADDING_X 10
#define ON_SCREEN_DEBUG_INFO_PADDING_Y 8
#define ON_SCREEN_DEBUG_INFO_LINE_GAP 4
#define ON_SCREEN_DEBUG_INFO_DEFAULT_LINE_HEIGHT 16
#define ON_SCREEN_DEBUG_INFO_DEFAULT_WIDTH 600
#define ON_SCREEN_DEBUG_INFO_DEFAULT_HEIGHT 400

/***************************************************************************/

/**
 * @brief Append one titled debug section to the combined on-screen buffer.
 * @param Destination Receives the concatenated text.
 * @param Title Section title ending with ':'.
 * @param Source Multi-line section body.
 */
static void OnScreenDebugInfoAppendSection(LPSTR Destination, LPCSTR Title, LPCSTR Source) {
    if (Destination == NULL || Title == NULL || Source == NULL || *Source == 0) {
        return;
    }

    if (*Destination != 0) {
        StringConcat(Destination, TEXT("\n"));
    }

    StringConcat(Destination, Title);
    StringConcat(Destination, TEXT("\n"));
    StringConcat(Destination, Source);
}

/***************************************************************************/

/**
 * @brief Return the preferred initial size for the on-screen debug component.
 * @param SizeOut Receives the preferred size.
 * @return TRUE on success.
 */
BOOL OnScreenDebugInfoGetPreferredSize(LPPOINT SizeOut) {
    if (SizeOut == NULL) return FALSE;

    SizeOut->X = ON_SCREEN_DEBUG_INFO_DEFAULT_WIDTH;
    SizeOut->Y = ON_SCREEN_DEBUG_INFO_DEFAULT_HEIGHT;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw up to five lines from one mutable multi-line text buffer.
 * @param GraphicsContext Target graphics context.
 * @param ClientRect Component client rectangle.
 * @param LineHeight Height used for each line.
 * @param Text Mutable text buffer split in-place on newline boundaries.
 */
static void OnScreenDebugInfoDrawLines(HANDLE GraphicsContext, LPRECT ClientRect, I32 LineHeight, LPSTR Text) {
    TEXT_DRAW_INFO DrawInfo;
    LPSTR Line = NULL;
    LPSTR NextLine = NULL;
    I32 LineY = 0;
    U32 LineIndex = 0;

    if (GraphicsContext == NULL || ClientRect == NULL || Text == NULL) return;

    DrawInfo = (TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GraphicsContext,
        .X = ClientRect->X1 + ON_SCREEN_DEBUG_INFO_PADDING_X,
        .Y = ClientRect->Y1 + ON_SCREEN_DEBUG_INFO_PADDING_Y,
        .Text = NULL,
        .Font = NULL
    };

    Line = Text;
    LineY = DrawInfo.Y;

    while (LineIndex < ON_SCREEN_DEBUG_INFO_LINE_COUNT && Line != NULL && *Line != 0) {
        NextLine = (LPSTR)strchr((const char*)Line, '\n');
        if (NextLine != NULL) {
            *NextLine = 0;
        }

        DrawInfo.Y = LineY;
        DrawInfo.Text = Line;
        (void)DrawText(&DrawInfo);

        LineIndex++;
        LineY += LineHeight + ON_SCREEN_DEBUG_INFO_LINE_GAP;

        if (NextLine == NULL) {
            break;
        }

        Line = NextLine + 1;
    }
}

/***************************************************************************/

/**
 * @brief Draw graphics debug lines inside the component window.
 * @param Window Component window handle.
 * @param Message Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return Message-specific result.
 */
U32 OnScreenDebugInfoWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    HANDLE GraphicsContext = NULL;
    TEXT_MEASURE_INFO MeasureInfo;
    DRIVER_DEBUG_INFO GraphicsDebugInfo;
    DRIVER_DEBUG_INFO MouseDebugInfo;
    DRIVER_DEBUG_INFO CombinedDebugInfo;
    I32 LineHeight = ON_SCREEN_DEBUG_INFO_DEFAULT_LINE_HEIGHT;

    switch (Message) {
        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);

            GraphicsContext = BeginWindowDraw(Window);
            if (GraphicsContext == NULL) {
                return 1;
            }

            if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
                (void)EndWindowDraw(Window);
                return 1;
            }

            MeasureInfo = (TEXT_MEASURE_INFO){
                .Header = {.Size = sizeof(TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
                .Text = TEXT("Line"),
                .Font = NULL,
                .Width = 0,
                .Height = 0
            };
            if (MeasureText(&MeasureInfo) != FALSE && MeasureInfo.Height != 0) {
                LineHeight = (I32)MeasureInfo.Height;
            }

            (void)SelectBrush(GraphicsContext, NULL);
            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_TEXT_NORMAL));

            GraphicsDebugInfo = (DRIVER_DEBUG_INFO){
                .Header = {.Size = sizeof(DRIVER_DEBUG_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0}
            };
            MouseDebugInfo = (DRIVER_DEBUG_INFO){
                .Header = {.Size = sizeof(DRIVER_DEBUG_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0}
            };
            CombinedDebugInfo = (DRIVER_DEBUG_INFO){
                .Header = {.Size = sizeof(DRIVER_DEBUG_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0}
            };
            StringClear(GraphicsDebugInfo.Text);
            StringClear(MouseDebugInfo.Text);
            StringClear(CombinedDebugInfo.Text);

            if (GetGraphicsDebugInfo(&GraphicsDebugInfo) == FALSE) {
                StringCopy(
                    GraphicsDebugInfo.Text,
                    TEXT("Manufacturer: unavailable\nProduct: unavailable\nResolution: 0x0x0"));
            }
            if (GetMouseDebugInfo(&MouseDebugInfo) == FALSE) {
                StringCopy(MouseDebugInfo.Text, TEXT("Manufacturer: unavailable\nProduct: unavailable"));
            }

            OnScreenDebugInfoAppendSection(CombinedDebugInfo.Text, TEXT("Graphics:"), GraphicsDebugInfo.Text);
            OnScreenDebugInfoAppendSection(CombinedDebugInfo.Text, TEXT("Mouse:"), MouseDebugInfo.Text);

            OnScreenDebugInfoDrawLines(GraphicsContext, &ClientRect, LineHeight, CombinedDebugInfo.Text);

            (void)EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
