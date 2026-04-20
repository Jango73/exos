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


    Desktop non-client rendering

\************************************************************************/

#include "Desktop-NonClient.h"
#include "Desktop-Private.h"
#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "text/CoreString.h"
#include "GFX.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID 0x53484252
#define DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID 0x000085A1
#define DESKTOP_NON_CLIENT_TITLE_BUTTON_MARGIN 4
#define DESKTOP_NON_CLIENT_TITLE_BUTTON_SPACING 4

/***************************************************************************/

typedef struct tag_DESKTOP_TITLE_BAR_BUTTON_SPEC {
    U32 Message;
    LPCSTR ElementID;
    LPCSTR Caption;
} DESKTOP_TITLE_BAR_BUTTON_SPEC, *LPDESKTOP_TITLE_BAR_BUTTON_SPEC;

/***************************************************************************/

static const DESKTOP_TITLE_BAR_BUTTON_SPEC DesktopTitleBarButtonSpecs[] = {
    {EWM_CLOSE, TEXT("window.button.close"), TEXT("x")},
    {EWM_MAXIMIZE, TEXT("window.button.maximize"), TEXT("+")},
    {EWM_MINIMIZE, TEXT("window.button.minimize"), TEXT("_")}};

/***************************************************************************/

/**
 * @brief Resolve whether one title bar button is visible for one style bitfield.
 * @param Style Window style bitfield.
 * @param Message Button action message.
 * @return TRUE when the button must be shown.
 */
static BOOL IsWindowTitleBarButtonVisible(U32 Style, U32 Message) {
    U32 VisibleMask;

    VisibleMask = Style & EWS_TITLE_BAR_BUTTONS_VISIBLE_MASK;
    if (VisibleMask == 0) return TRUE;

    switch (Message) {
        case EWM_CLOSE:
            return ((Style & EWS_CLOSE_BUTTON_VISIBLE) != 0);

        case EWM_MINIMIZE:
            return ((Style & EWS_MINIMIZE_BUTTON_VISIBLE) != 0);

        case EWM_MAXIMIZE:
            return ((Style & EWS_MAXIMIZE_BUTTON_VISIBLE) != 0);
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Draw one filled rectangle using an explicit color.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Fill color.
 * @param CornerRadius Corner radius.
 * @return TRUE on success.
 */
static BOOL DrawSolidRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color, U32 CornerStyle, U32 CornerRadius) {
    RECT_INFO RectInfo;
    BRUSH Brush;
    HANDLE OldPen;
    HANDLE OldBrush;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    MemorySet(&Brush, 0, sizeof(Brush));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;
    RectInfo.CornerRadius = (I32)CornerRadius;
    RectInfo.CornerStyle = CornerStyle;

    OldPen = SelectPen(GC, NULL);
    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    (void)KernelRectangle(&RectInfo);
    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one vertical gradient rectangle.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param StartColor Gradient start color (top).
 * @param EndColor Gradient end color (bottom).
 * @param CornerRadius Corner radius.
 * @return TRUE on success.
 */
static BOOL DrawVerticalGradientRect(
    HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor, U32 CornerStyle, U32 CornerRadius) {
    RECT_INFO RectInfo;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = RECT_FLAG_FILL_VERTICAL_GRADIENT;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;
    RectInfo.StartColor = StartColor;
    RectInfo.EndColor = EndColor;
    RectInfo.CornerRadius = (I32)CornerRadius;
    RectInfo.CornerStyle = CornerStyle;

    return KernelRectangle(&RectInfo);
}

/***************************************************************************/

/**
 * @brief Resolve themed title bar height.
 * @param TitleHeightOut Receives the title bar height in pixels.
 * @return TRUE on success.
 */
static BOOL ResolveWindowTitleBarHeight(U32* TitleHeightOut) {
    U32 TitleHeight = 22;

    if (TitleHeightOut == NULL) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("title_height"), &TitleHeight)) {
        if (!DesktopThemeResolveTokenMetricByName(TEXT("metric.window.title_height"), &TitleHeight)) {
            TitleHeight = 22;
        }
    }

    *TitleHeightOut = TitleHeight;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one title bar rectangle from one base window rectangle.
 * @param BaseRect Input rectangle in local or screen coordinates.
 * @param TitleRect Receives the title bar rectangle in matching coordinates.
 * @return TRUE on success.
 */
static BOOL ResolveWindowTitleBarRect(LPRECT BaseRect, LPRECT TitleRect) {
    U32 TitleHeight = 22;
    I32 MaxTitleHeight;

    if (BaseRect == NULL || TitleRect == NULL) return FALSE;
    if (BaseRect->X1 > BaseRect->X2 || BaseRect->Y1 > BaseRect->Y2) return FALSE;
    (void)ResolveWindowTitleBarHeight(&TitleHeight);
    if (TitleHeight == 0) return FALSE;

    MaxTitleHeight = BaseRect->Y2 - BaseRect->Y1 + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;

    TitleRect->X1 = BaseRect->X1;
    TitleRect->Y1 = BaseRect->Y1;
    TitleRect->X2 = BaseRect->X2;
    TitleRect->Y2 = BaseRect->Y1 + (I32)TitleHeight - 1;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve the three system title bar button rectangles.
 * @param TitleRect Title bar rectangle in one coordinate space.
 * @param ButtonRectsOut Receives rectangles ordered like `DesktopTitleBarButtonSpecs`.
 * @return TRUE on success.
 */
static BOOL ResolveWindowTitleBarButtonRects(LPRECT TitleRect, LPRECT ButtonRectsOut) {
    I32 Height;
    I32 ButtonSize;
    I32 ButtonTop;
    I32 ButtonRight;
    U32 ButtonSizeMetric = 0;
    U32 ButtonSpacing = DESKTOP_NON_CLIENT_TITLE_BUTTON_SPACING;
    U32 TitleBarPadding = DESKTOP_NON_CLIENT_TITLE_BUTTON_MARGIN;
    UINT Index;

    if (TitleRect == NULL || ButtonRectsOut == NULL) return FALSE;
    if (TitleRect->X1 > TitleRect->X2 || TitleRect->Y1 > TitleRect->Y2) return FALSE;

    Height = TitleRect->Y2 - TitleRect->Y1 + 1;
    if (Height <= 0) return FALSE;

    (void)DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("button_spacing"), &ButtonSpacing);
    (void)DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("padding"), &TitleBarPadding);
    if (DesktopThemeResolveLevel1Metric(DesktopTitleBarButtonSpecs[0].ElementID, TEXT("normal"), TEXT("button_size"), &ButtonSizeMetric) ==
        FALSE) {
        ButtonSize = Height - ((I32)TitleBarPadding * 2);
    } else {
        ButtonSize = (I32)ButtonSizeMetric;
    }
    if (ButtonSize < 8) ButtonSize = Height - 2;
    if (ButtonSize <= 0) return FALSE;

    ButtonTop = TitleRect->Y1 + ((Height - ButtonSize) / 2);
    ButtonRight = TitleRect->X2 - (I32)TitleBarPadding;

    for (Index = 0; Index < sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0]); Index++) {
        ButtonRectsOut[Index].X2 = ButtonRight;
        ButtonRectsOut[Index].Y1 = ButtonTop;
        ButtonRectsOut[Index].X1 = ButtonRight - ButtonSize + 1;
        ButtonRectsOut[Index].Y2 = ButtonTop + ButtonSize - 1;
        ButtonRight = ButtonRectsOut[Index].X1 - (I32)ButtonSpacing - 1;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one visible title bar button rectangle array.
 * @param Style Window style bitfield.
 * @param TitleRect Title bar rectangle in one coordinate space.
 * @param ButtonRectsOut Receives visible button rectangles packed from right to left.
 * @param ButtonMessagesOut Receives matching button messages.
 * @param ButtonCountOut Receives number of visible buttons.
 * @return TRUE on success.
 */
static BOOL ResolveVisibleWindowTitleBarButtons(
    U32 Style, LPRECT TitleRect, LPRECT ButtonRectsOut, U32* ButtonMessagesOut, UINT* ButtonCountOut) {
    RECT AllButtonRects[sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0])];
    UINT SourceIndex;
    UINT TargetIndex;

    if (TitleRect == NULL || ButtonRectsOut == NULL || ButtonMessagesOut == NULL || ButtonCountOut == NULL) return FALSE;
    if (ResolveWindowTitleBarButtonRects(TitleRect, AllButtonRects) == FALSE) return FALSE;

    TargetIndex = 0;
    for (SourceIndex = 0; SourceIndex < sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0]); SourceIndex++) {
        if (IsWindowTitleBarButtonVisible(Style, DesktopTitleBarButtonSpecs[SourceIndex].Message) == FALSE) continue;

        ButtonRectsOut[TargetIndex] = AllButtonRects[TargetIndex];
        ButtonMessagesOut[TargetIndex] = DesktopTitleBarButtonSpecs[SourceIndex].Message;
        TargetIndex++;
    }

    *ButtonCountOut = TargetIndex;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one button spec from its action message.
 * @param Message Button action message.
 * @return Matching spec or NULL.
 */
static const DESKTOP_TITLE_BAR_BUTTON_SPEC* FindWindowTitleBarButtonSpec(U32 Message) {
    UINT Index;

    for (Index = 0; Index < sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0]); Index++) {
        if (DesktopTitleBarButtonSpecs[Index].Message == Message) return &DesktopTitleBarButtonSpecs[Index];
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Draw one themed title bar button.
 * @param Window Target window.
 * @param GC Target graphics context.
 * @param ButtonRect Button rectangle in local coordinates.
 * @param Spec Button specification.
 * @param StateID Theme state identifier.
 * @param FallbackGlyphColor Fallback glyph color.
 * @return TRUE on success.
 */
static BOOL DrawWindowTitleBarButton(
    HANDLE Window, HANDLE GC, LPRECT ButtonRect, const DESKTOP_TITLE_BAR_BUTTON_SPEC* Spec, LPCSTR StateID, COLOR FallbackGlyphColor) {
    STR Glyph[32];
    COLOR BackgroundColor = 0;
    COLOR BackgroundColor2 = 0;
    COLOR BorderColor = 0;
    COLOR GlyphColor = 0;
    GFX_TEXT_MEASURE_INFO MeasureInfo;
    GFX_TEXT_DRAW_INFO DrawInfo;
    U32 CornerStyle = RECT_CORNER_STYLE_SQUARE;
    U32 CornerRadiusMetric = 0;
    U32 CornerRadiusLimitMetric = 0;
    U32 BorderThickness = 0;
    I32 CornerRadius = 0;
    I32 TextX;
    I32 TextY;
    HANDLE OldPen = NULL;
    HANDLE OldBrush = NULL;
    BOOL HasBackground;
    BOOL HasBackground2;
    BOOL HasBorderColor;
    BOOL HasBorderThickness;

    if (Window == NULL || GC == NULL || ButtonRect == NULL || Spec == NULL || StateID == NULL) return FALSE;
    UNUSED(Window);

    if (!DesktopThemeResolveLevel1Text(Spec->ElementID, StateID, TEXT("glyph"), Glyph, sizeof(Glyph))) {
        StringCopy(Glyph, Spec->Caption);
    }

    if (!DesktopThemeResolveLevel1Color(Spec->ElementID, StateID, TEXT("glyph_color"), &GlyphColor)) {
        GlyphColor = FallbackGlyphColor;
    }

    HasBackground = DesktopThemeResolveLevel1Color(Spec->ElementID, StateID, TEXT("background"), &BackgroundColor);
    HasBackground2 = DesktopThemeResolveLevel1Color(Spec->ElementID, StateID, TEXT("background2"), &BackgroundColor2);
    HasBorderColor = DesktopThemeResolveLevel1Color(Spec->ElementID, StateID, TEXT("border_color"), &BorderColor);
    HasBorderThickness = DesktopThemeResolveLevel1Metric(Spec->ElementID, StateID, TEXT("border_thickness"), &BorderThickness);

    MeasureInfo = (GFX_TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Glyph,
        .Font = NULL,
        .Width = 0,
        .Height = 0};
    (void)DesktopMeasureText(&MeasureInfo);

    TextX = ButtonRect->X1 + (((ButtonRect->X2 - ButtonRect->X1 + 1) - (I32)MeasureInfo.Width) / 2);
    TextY = ButtonRect->Y1 + (((ButtonRect->Y2 - ButtonRect->Y1 + 1) - (I32)MeasureInfo.Height) / 2);
    if (TextY < ButtonRect->Y1) TextY = ButtonRect->Y1;

    if (DesktopThemeResolveLevel1CornerStyle(Spec->ElementID, StateID, TEXT("corner_style"), &CornerStyle) != FALSE &&
        DesktopThemeResolveLevel1Metric(Spec->ElementID, StateID, TEXT("corner_radius"), &CornerRadiusMetric) != FALSE) {
        if ((I32)CornerRadiusMetric == RECT_CORNER_RADIUS_AUTO &&
            DesktopThemeResolveLevel1Metric(Spec->ElementID, StateID, TEXT("corner_radius_limit"), &CornerRadiusLimitMetric) != FALSE) {
            CornerRadius = RECT_CORNER_RADIUS_AUTO_LIMIT((I32)CornerRadiusLimitMetric);
        } else {
            CornerRadius = (I32)CornerRadiusMetric;
        }
    }

    if (HasBackground != FALSE) {
        if (HasBackground2 != FALSE && BackgroundColor2 != BackgroundColor) {
            (void)DrawVerticalGradientRect(
                GC, ButtonRect->X1, ButtonRect->Y1, ButtonRect->X2, ButtonRect->Y2, BackgroundColor, BackgroundColor2, CornerStyle, (U32)CornerRadius);
        } else {
            (void)DrawSolidRect(
                GC, ButtonRect->X1, ButtonRect->Y1, ButtonRect->X2, ButtonRect->Y2, BackgroundColor, CornerStyle, (U32)CornerRadius);
        }
    }

    if (HasBorderColor != FALSE && HasBorderThickness != FALSE && BorderThickness > 0) {
        UINT BorderIndex;

        for (BorderIndex = 0; BorderIndex < BorderThickness; BorderIndex++) {
            (void)DrawSolidRect(
                GC,
                ButtonRect->X1 + (I32)BorderIndex,
                ButtonRect->Y1 + (I32)BorderIndex,
                ButtonRect->X2 - (I32)BorderIndex,
                ButtonRect->Y1 + (I32)BorderIndex,
                BorderColor,
                RECT_CORNER_STYLE_SQUARE,
                0);
            (void)DrawSolidRect(
                GC,
                ButtonRect->X1 + (I32)BorderIndex,
                ButtonRect->Y2 - (I32)BorderIndex,
                ButtonRect->X2 - (I32)BorderIndex,
                ButtonRect->Y2 - (I32)BorderIndex,
                BorderColor,
                RECT_CORNER_STYLE_SQUARE,
                0);
            (void)DrawSolidRect(
                GC,
                ButtonRect->X1 + (I32)BorderIndex,
                ButtonRect->Y1 + (I32)BorderIndex,
                ButtonRect->X1 + (I32)BorderIndex,
                ButtonRect->Y2 - (I32)BorderIndex,
                BorderColor,
                RECT_CORNER_STYLE_SQUARE,
                0);
            (void)DrawSolidRect(
                GC,
                ButtonRect->X2 - (I32)BorderIndex,
                ButtonRect->Y1 + (I32)BorderIndex,
                ButtonRect->X2 - (I32)BorderIndex,
                ButtonRect->Y2 - (I32)BorderIndex,
                BorderColor,
                RECT_CORNER_STYLE_SQUARE,
                0);
        }
    }

    OldPen = SelectPen(GC, GetSystemPen(SM_COLOR_TITLE_TEXT));
    OldBrush = SelectBrush(GC, NULL);
    if (GlyphColor != 0) {
        PEN TempPen;

        MemorySet(&TempPen, 0, sizeof(TempPen));
        TempPen.TypeID = KOID_PEN;
        TempPen.References = 1;
        TempPen.Color = GlyphColor;
        TempPen.Pattern = MAX_U32;
        (void)SelectPen(GC, (HANDLE)&TempPen);
    }

    DrawInfo = (GFX_TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GC,
        .X = TextX,
        .Y = TextY,
        .Text = Glyph,
        .Font = NULL};
    (void)DesktopDrawText(&DrawInfo);

    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one title bar button message from one point.
 * @param Window Target window.
 * @param ScreenPoint Screen-space point to test.
 * @return One `EWM_*` button message or `EWM_NONE`.
 */
U32 GetWindowTitleBarButtonMessageAtPoint(LPWINDOW Window, LPPOINT ScreenPoint) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    RECT TitleRect;
    RECT ButtonRects[sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0])];
    U32 ButtonMessages[sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0])];
    UINT Index;
    UINT ButtonCount;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return EWM_NONE;
    if (ScreenPoint == NULL) return EWM_NONE;
    if (ShouldDrawWindowNonClient(Window) == FALSE) return EWM_NONE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return EWM_NONE;
    if (ResolveWindowTitleBarRect(&(Window->ScreenRect), &TitleRect) == FALSE) return EWM_NONE;
    if (ResolveVisibleWindowTitleBarButtons(Snapshot.Style, &TitleRect, ButtonRects, ButtonMessages, &ButtonCount) == FALSE) {
        return EWM_NONE;
    }

    for (Index = 0; Index < ButtonCount; Index++) {
        if (ScreenPoint->X < ButtonRects[Index].X1 || ScreenPoint->X > ButtonRects[Index].X2) continue;
        if (ScreenPoint->Y < ButtonRects[Index].Y1 || ScreenPoint->Y > ButtonRects[Index].Y2) continue;
        return ButtonMessages[Index];
    }

    return EWM_NONE;
}

/***************************************************************************/

/**
 * @brief Resolve themed border thickness for window border rendering.
 * @param ThicknessOut Receives thickness in pixels.
 * @return TRUE on success.
 */
static BOOL ResolveWindowBorderThickness(U32* ThicknessOut) {
    U32 BorderThickness;

    if (ThicknessOut == NULL) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.border"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
        BorderThickness = 2;
    }

    if (BorderThickness == 0) BorderThickness = 1;
    *ThicknessOut = BorderThickness;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed window borders around a window rectangle.
 * @param GC Graphics context.
 * @param Rect Target window rectangle in window coordinates.
 */
static void DrawWindowBorderFromTheme(HANDLE GC, LPRECT Rect) {
    U32 TitleHeight = 22;
    COLOR BorderColor = 0;
    LINE_INFO LineInfo;
    PEN Pen;
    HANDLE OldPen;
    I32 Width;
    I32 Height;
    I32 MaxThickness;
    I32 Thickness;
    I32 BorderTopY;

    if (GC == NULL || Rect == NULL) return;

    (void)ResolveWindowTitleBarHeight(&TitleHeight);

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
        HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
        SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
            BorderColor = ((LPPEN)Pen)->Color;
        }
    }

    Width = Rect->X2 - Rect->X1 + 1;
    Height = Rect->Y2 - Rect->Y1 + 1;
    if (Width <= 0 || Height <= 0) return;

    MaxThickness = Width < Height ? Width : Height;
    MaxThickness /= 2;
    if (MaxThickness <= 0) return;

    Thickness = 3;
    if (Thickness <= 0) return;
    if (Thickness > MaxThickness) Thickness = MaxThickness;
    BorderTopY = Rect->Y1 + (I32)TitleHeight;
    if (BorderTopY < Rect->Y1) BorderTopY = Rect->Y1;
    if (BorderTopY > Rect->Y2 + 1) BorderTopY = Rect->Y2 + 1;

    MemorySet(&Pen, 0, sizeof(Pen));
    Pen.TypeID = KOID_PEN;
    Pen.References = 1;
    Pen.Color = BorderColor;
    Pen.Pattern = MAX_U32;
    Pen.Width = (U32)Thickness;

    LineInfo.Header.Size = sizeof(LineInfo);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GC;

    OldPen = SelectPen(GC, (HANDLE)&Pen);

    LineInfo.X1 = Rect->X1 + ((Thickness - 1) / 2);
    LineInfo.Y1 = BorderTopY;
    LineInfo.X2 = Rect->X1 + ((Thickness - 1) / 2);
    LineInfo.Y2 = Rect->Y2 - (Thickness / 2);
    if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);

    LineInfo.X1 = Rect->X2 - (Thickness / 2);
    LineInfo.Y1 = BorderTopY;
    LineInfo.X2 = Rect->X2 - (Thickness / 2);
    LineInfo.Y2 = Rect->Y2 - (Thickness / 2);
    if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);

    LineInfo.X1 = Rect->X1 + ((Thickness - 1) / 2);
    LineInfo.Y1 = Rect->Y2 - (Thickness / 2);
    LineInfo.X2 = Rect->X2 - (Thickness / 2);
    LineInfo.Y2 = Rect->Y2 - (Thickness / 2);
    if (LineInfo.X1 <= LineInfo.X2) (void)Line(&LineInfo);

    (void)SelectPen(GC, OldPen);
}

/***************************************************************************/

/**
 * @brief Draw a themed title bar in the non-client frame area.
 * @param Window Window handle.
 * @param GC Graphics context.
 * @param Rect Window-local rectangle.
 * @return TRUE when title bar was drawn.
 */
static BOOL DrawWindowTitleBarFromTheme(HANDLE Window, HANDLE GC, LPRECT Rect) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    LPCSTR TitleState = TEXT("normal");
    HANDLE TitleBrush;
    HANDLE TitleBrush2;
    HANDLE TitlePen;
    HANDLE OldPen = NULL;
    HANDLE OldBrush = NULL;
    LPBRUSH TitleBrushPtr;
    LPBRUSH TitleBrush2Ptr;
    COLOR Background = 0;
    COLOR Background2 = 0;
    COLOR TextColor = 0;
    U32 TitleHeight = 22;
    U32 CornerRadius = 6;
    U32 CornerStyle = RECT_CORNER_STYLE_ROUNDED;
    I32 InnerX1;
    I32 InnerX2;
    I32 InnerY1;
    I32 InnerY2;
    I32 BottomLineY;
    I32 TextHeight = 0;
    I32 TextY;
    COLOR SeparatorColor = 0;
    GFX_TEXT_MEASURE_INFO MeasureInfo;
    GFX_TEXT_DRAW_INFO DrawInfo;
    RECT TitleRect;
    RECT ButtonRects[sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0])];
    U32 ButtonMessages[sizeof(DesktopTitleBarButtonSpecs) / sizeof(DesktopTitleBarButtonSpecs[0])];
    UINT ButtonIndex;
    UINT ButtonCount;
    U32 PressedMessage;

    if (GC == NULL || Rect == NULL) return FALSE;
    if (GetWindowStateSnapshot((LPWINDOW)Window, &Snapshot) == FALSE) return FALSE;
    PressedMessage = GetWindowProp(Window, TEXT("desktop.non_client.pressed_message"));
    if (IsDesktopWindowFocused((LPWINDOW)Window) != FALSE) {
        TitleState = TEXT("focused");
    }

    (void)ResolveWindowTitleBarHeight(&TitleHeight);
    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TitleState, TEXT("corner_radius"), &CornerRadius)) {
        CornerRadius = 6;
    }
    if (!DesktopThemeResolveLevel1CornerStyle(TEXT("window.titlebar"), TitleState, TEXT("corner_style"), &CornerStyle)) {
        CornerStyle = CornerRadius > 0 ? RECT_CORNER_STYLE_ROUNDED : RECT_CORNER_STYLE_SQUARE;
    }
    if (TitleHeight == 0) return FALSE;

    if (ResolveWindowTitleBarRect(Rect, &TitleRect) == FALSE) return FALSE;

    InnerX1 = TitleRect.X1;
    InnerX2 = TitleRect.X2;
    InnerY1 = TitleRect.Y1;
    if (InnerX1 > InnerX2 || InnerY1 > Rect->Y2) return FALSE;
    InnerY2 = TitleRect.Y2;

    SAFE_USE_VALID_ID((LPWINDOW)Window, KOID_WINDOW) {
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TitleState, TEXT("background"), &Background)) {
        if (StringCompareNC(TitleState, TEXT("focused")) == 0) {
            if (!DesktopThemeResolveTokenColorByName(TEXT("color.window.title.focused.start"), &Background)) {
                Background = SETALPHA(COLOR_GRAY50, 0xFF);
            }
        } else {
            TitleBrush = GetSystemBrush(SM_COLOR_TITLE_BAR);
            SAFE_USE_VALID_ID((LPBRUSH)TitleBrush, KOID_BRUSH) {
                TitleBrushPtr = (LPBRUSH)TitleBrush;
                Background = TitleBrushPtr->Color;
            }
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TitleState, TEXT("background2"), &Background2)) {
        if (StringCompareNC(TitleState, TEXT("focused")) == 0) {
            if (!DesktopThemeResolveTokenColorByName(TEXT("color.window.title.focused.end"), &Background2)) {
                Background2 = SETALPHA(COLOR_GRAY40, 0xFF);
            }
        } else {
            TitleBrush2 = GetSystemBrush(SM_COLOR_TITLE_BAR_2);
            SAFE_USE_VALID_ID((LPBRUSH)TitleBrush2, KOID_BRUSH) {
                TitleBrush2Ptr = (LPBRUSH)TitleBrush2;
                Background2 = TitleBrush2Ptr->Color;
            }
        }
    }

    if (Background != 0 || Background2 != 0) {
        if (Background2 != 0 && Background2 != Background) {
            (void)DrawVerticalGradientRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background, Background2, CornerStyle, CornerRadius);
        } else {
            (void)DrawSolidRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background, CornerStyle, CornerRadius);
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &SeparatorColor)) {
        HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
        SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
            SeparatorColor = ((LPPEN)Pen)->Color;
        }
    }

    BottomLineY = InnerY2;
    if (BottomLineY >= Rect->Y1 && BottomLineY <= Rect->Y2) {
        (void)DrawSolidRect(GC, InnerX1, BottomLineY, InnerX2, BottomLineY, SeparatorColor, RECT_CORNER_STYLE_SQUARE, 0);
    }

    if (ResolveVisibleWindowTitleBarButtons(Snapshot.Style, &TitleRect, ButtonRects, ButtonMessages, &ButtonCount) != FALSE) {
        for (ButtonIndex = 0; ButtonIndex < ButtonCount; ButtonIndex++) {
            const DESKTOP_TITLE_BAR_BUTTON_SPEC* Spec;
            LPCSTR ButtonState;

            Spec = FindWindowTitleBarButtonSpec(ButtonMessages[ButtonIndex]);
            if (Spec == NULL) continue;
            ButtonState = (PressedMessage == ButtonMessages[ButtonIndex]) ? TEXT("pressed") : TEXT("normal");
            (void)DrawWindowTitleBarButton(Window, GC, &ButtonRects[ButtonIndex], Spec, ButtonState, TextColor);
        }
    }

    if (StringLength(Snapshot.Caption) == 0) return TRUE;

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TitleState, TEXT("text_color"), &TextColor)) {
        TitlePen = GetSystemPen(SM_COLOR_TITLE_TEXT);
        SAFE_USE_VALID_ID((LPPEN)TitlePen, KOID_PEN) {
            TextColor = ((LPPEN)TitlePen)->Color;
        }
    }

    MeasureInfo = (GFX_TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Snapshot.Caption,
        .Font = NULL,
        .Width = 0,
        .Height = 0
    };
    if (DesktopMeasureText(&MeasureInfo) != FALSE && MeasureInfo.Height != 0) {
        TextHeight = (I32)MeasureInfo.Height;
    } else {
        TextHeight = 16;
    }

    TextY = InnerY1 + (((InnerY2 - InnerY1 + 1) - TextHeight) / 2);
    if (TextY < InnerY1) TextY = InnerY1;

    OldPen = SelectPen(GC, GetSystemPen(SM_COLOR_TITLE_TEXT));
    OldBrush = SelectBrush(GC, NULL);
    if (TextColor != 0) {
        PEN TempPen;

        MemorySet(&TempPen, 0, sizeof(TempPen));
        TempPen.TypeID = KOID_PEN;
        TempPen.References = 1;
        TempPen.Color = TextColor;
        TempPen.Pattern = MAX_U32;
        (void)SelectPen(GC, (HANDLE)&TempPen);
    }

    DrawInfo = (GFX_TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GC,
        .X = InnerX1 + 8,
        .Y = TextY,
        .Text = Snapshot.Caption,
        .Font = NULL
    };
    (void)DesktopDrawText(&DrawInfo);

    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed client area inside the non-client border.
 * @param Window Window handle.
 * @param GC Graphics context.
 * @param Rect Full window rectangle in window coordinates.
 * @return TRUE when a client area was drawn.
 */
BOOL DrawWindowClientArea(HANDLE Window, HANDLE GC, LPRECT Rect) {
    RECT ClientRect;

    if (Window == NULL || GC == NULL || Rect == NULL) return FALSE;
    if (GetWindowClientRectFromWindowRect((LPWINDOW)Window, Rect, &ClientRect) == FALSE) return FALSE;

    return DrawWindowBackground(Window, GC, &ClientRect, THEME_TOKEN_WINDOW_BACKGROUND_CLIENT);
}

/***************************************************************************/

/**
 * @brief Resolve decoration mode from a window style bitfield.
 * @param Style Window style bitfield.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
static U32 GetDecorationModeFromStyle(U32 Style) {
    if (Style & EWS_BARE_SURFACE) return WINDOW_DECORATION_MODE_BARE;
    if (Style & EWS_CLIENT_DECORATED) return WINDOW_DECORATION_MODE_CLIENT;

    // Compatibility: undecorated style bitfield defaults to system decorations.
    return WINDOW_DECORATION_MODE_SYSTEM;
}

/***************************************************************************/

/**
 * @brief Resolve the decoration mode configured on a window.
 * @param Window Window to inspect.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
U32 GetWindowDecorationMode(LPWINDOW Window) {
    if (Window == NULL) return WINDOW_DECORATION_MODE_SYSTEM;
    if (Window->TypeID != KOID_WINDOW) return WINDOW_DECORATION_MODE_SYSTEM;

    return GetDecorationModeFromStyle(Window->Style);
}

/***************************************************************************/

/**
 * @brief Tell whether the kernel non-client renderer should draw this window.
 * @param Window Window to inspect.
 * @return TRUE when system decorations are enabled.
 */
BOOL ShouldDrawWindowNonClient(LPWINDOW Window) {
    U32 DecorationMode;
    BOOL Result;

    DecorationMode = GetWindowDecorationMode(Window);
    Result = (DecorationMode == WINDOW_DECORATION_MODE_SYSTEM);

    if (Window != NULL && Window->TypeID == KOID_WINDOW &&
        (Window->WindowID == DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID ||
         Window->WindowID == DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID)) {
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Tell whether one screen point lies in one window title bar.
 * @param Window Target window.
 * @param ScreenPoint Point in screen coordinates.
 * @return TRUE when the point is inside the window title bar area.
 */
BOOL IsPointInWindowTitleBar(LPWINDOW Window, LPPOINT ScreenPoint) {
    RECT ScreenRect;
    RECT TitleRect;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenPoint == NULL) return FALSE;
    if (ShouldDrawWindowNonClient(Window) == FALSE) return FALSE;

    ScreenRect = Window->ScreenRect;
    if (ResolveWindowTitleBarRect(&ScreenRect, &TitleRect) == FALSE) return FALSE;
    if (ScreenPoint->X < TitleRect.X1 || ScreenPoint->X > TitleRect.X2) return FALSE;
    if (ScreenPoint->Y < TitleRect.Y1 || ScreenPoint->Y > TitleRect.Y2) return FALSE;
    if (GetWindowTitleBarButtonMessageAtPoint(Window, ScreenPoint) != EWM_NONE) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute the client rectangle from a window-rectangle.
 * @param Window Target window.
 * @param WindowRect Full window rectangle (window coordinates).
 * @param ClientRect Receives client rectangle (window coordinates).
 * @return TRUE when a valid client area was produced.
 */
BOOL GetWindowClientRectFromWindowRect(LPWINDOW Window, LPRECT WindowRect, LPRECT ClientRect) {
    U32 BorderThickness;
    U32 TitleHeight = 22;
    I32 Left;
    I32 Top;
    I32 Right;
    I32 Bottom;
    I32 MaxTitleHeight;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL || ClientRect == NULL) return FALSE;

    Left = WindowRect->X1;
    Top = WindowRect->Y1;
    Right = WindowRect->X2;
    Bottom = WindowRect->Y2;

    if (Left > Right || Top > Bottom) return FALSE;

    if (ShouldDrawWindowNonClient(Window) == FALSE) {
        *ClientRect = *WindowRect;
        return TRUE;
    }

    if (ResolveWindowBorderThickness(&BorderThickness) == FALSE) return FALSE;
    (void)ResolveWindowTitleBarHeight(&TitleHeight);

    Left += (I32)BorderThickness;
    Right -= (I32)BorderThickness;
    Bottom -= (I32)BorderThickness;

    if (Left > Right || Top > Bottom) return FALSE;

    MaxTitleHeight = Bottom - Top + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;
    Top += (I32)TitleHeight;

    if (Top > Bottom) return FALSE;

    ClientRect->X1 = Left;
    ClientRect->Y1 = Top;
    ClientRect->X2 = Right;
    ClientRect->Y2 = Bottom;

    return TRUE;
}

/***************************************************************************/

BOOL GetWindowClientRect(HANDLE Handle, LPRECT ClientRect) {
    RECT WindowRect;
    LPWINDOW Window = (LPWINDOW)Handle;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClientRect == NULL) return FALSE;
    if (GetWindowRect(Handle, &WindowRect) == FALSE) return FALSE;

    return GetWindowClientRectFromWindowRect(Window, &WindowRect, ClientRect);
}

/***************************************************************************/

/**
 * @brief Draw the default non-client visuals for a window.
 * @param Window Window handle.
 * @param GC Graphics context handle.
 * @param Rect Window-local rectangle to draw.
 * @return TRUE when drawing was performed.
 */
BOOL DrawWindowNonClient(HANDLE Window, HANDLE GC, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Window;

    if (Window == NULL) return FALSE;
    if (GC == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        if (This->WindowID == DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID ||
            This->WindowID == DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID) {
        }
    }

    (void)DrawWindowTitleBarFromTheme(Window, GC, Rect);
    DrawWindowBorderFromTheme(GC, Rect);

    return TRUE;
}

/***************************************************************************/
