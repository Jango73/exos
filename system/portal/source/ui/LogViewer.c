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


    Desktop log viewer component

\************************************************************************/

#include "ui/LogViewer.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>


/***************************************************************************/

#define LOG_VIEWER_TIMER_ID 1
#define LOG_VIEWER_TIMER_INTERVAL_MS 1000
#define LOG_VIEWER_PADDING_X 8
#define LOG_VIEWER_PADDING_Y 4
#define LOG_VIEWER_LINE_GAP 2
#define LOG_VIEWER_DEFAULT_WIDTH 720
#define LOG_VIEWER_DEFAULT_HEIGHT 360
#define LOG_VIEWER_TEXT_BUFFER_SIZE 32768
#define LOG_VIEWER_PROP_SEQUENCE TEXT("desktop.logviewer.sequence")

BOOL LogViewerEnsureClassRegistered(void) {
    if (FindWindowClass(DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME, 0, NULL, LogViewerWindowFunc, 0) != NULL;
}

/***************************************************************************/

/**
 * @brief Draw each line from one mutable multi-line text buffer.
 * @param GraphicsContext Target graphics context.
 * @param ClientRect Window client rectangle.
 * @param LineHeight Current text line height.
 * @param Text Mutable text buffer split in place on newline boundaries.
 */
static void LogViewerDrawLines(HANDLE GraphicsContext, LPRECT ClientRect, I32 LineHeight, LPSTR Text) {
    TEXT_DRAW_INFO DrawInfo;
    LPSTR Line;
    LPSTR NextLine;
    I32 LineY;

    if (GraphicsContext == NULL || ClientRect == NULL || Text == NULL) return;

    DrawInfo = (TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GraphicsContext,
        .X = ClientRect->X1 + LOG_VIEWER_PADDING_X,
        .Y = ClientRect->Y1 + LOG_VIEWER_PADDING_Y,
        .Text = NULL,
        .Font = NULL};

    Line = Text;
    LineY = DrawInfo.Y;

    while (Line != NULL && *Line != 0) {
        NextLine = (LPSTR)strchr((const char*)Line, '\n');
        if (NextLine != NULL) {
            *NextLine = 0;
        }

        DrawInfo.Y = LineY;
        DrawInfo.Text = Line;
        (void)DrawText(&DrawInfo);
        LineY += LineHeight + LOG_VIEWER_LINE_GAP;

        if (NextLine == NULL) {
            break;
        }

        Line = NextLine + 1;
    }
}

U32 LogViewerWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    HANDLE GraphicsContext;
    TEXT_MEASURE_INFO MeasureInfo;
    KERNEL_LOG_RECENT_INFO View;
    STR TextBuffer[LOG_VIEWER_TEXT_BUFFER_SIZE];
    I32 ClientHeight;
    I32 AvailableHeight;
    I32 LineHeight;
    UINT VisibleLines;
    U32 Sequence;

    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowProp(Window, LOG_VIEWER_PROP_SEQUENCE, KernelLogGetRecentSequence());
            (void)SetWindowTimer(Window, LOG_VIEWER_TIMER_ID, LOG_VIEWER_TIMER_INTERVAL_MS);
            return 1;

        case EWM_DELETE:
            (void)KillWindowTimer(Window, LOG_VIEWER_TIMER_ID);
            break;

        case EWM_TIMER:
            if (Param1 == LOG_VIEWER_TIMER_ID) {
                Sequence = KernelLogGetRecentSequence();
                if (Sequence != GetWindowProp(Window, LOG_VIEWER_PROP_SEQUENCE)) {
                    (void)SetWindowProp(Window, LOG_VIEWER_PROP_SEQUENCE, Sequence);
                    (void)InvalidateClientRect(Window, NULL);
                }
            }
            return 1;

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
                .Height = 0};
            LineHeight = 16;
            if (MeasureText(&MeasureInfo) != FALSE && MeasureInfo.Height != 0) {
                LineHeight = (I32)MeasureInfo.Height;
            }

            ClientHeight = ClientRect.Y2 - ClientRect.Y1 + 1;
            AvailableHeight = ClientHeight - (LOG_VIEWER_PADDING_Y * 2);
            VisibleLines = 1;
            if (AvailableHeight > 0) {
                VisibleLines = (UINT)(AvailableHeight / (LineHeight + LOG_VIEWER_LINE_GAP));
                if (VisibleLines == 0) VisibleLines = 1;
            }

            View.Text = TextBuffer;
            View.TextBufferSize = sizeof(TextBuffer);
            View.MaxLines = VisibleLines;
            if (KernelLogCaptureRecentLines(&View) == FALSE) {
                StringCopy(TextBuffer, TEXT("Log unavailable\n"));
            }

            (void)SelectBrush(GraphicsContext, NULL);
            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_TEXT_NORMAL));
            LogViewerDrawLines(GraphicsContext, &ClientRect, LineHeight, TextBuffer);

            (void)EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
