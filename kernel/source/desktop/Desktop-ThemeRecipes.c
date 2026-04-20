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


    Desktop theme recipe renderer

\************************************************************************/

#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeSchema.h"
#include "Desktop-ThemeTokens.h"
#include "text/CoreString.h"
#include "GFX.h"
#include "core/Kernel.h"

/***************************************************************************/
// Macros

#define THEME_RECIPE_OP_FILL_RECT 0x00000001
#define THEME_RECIPE_OP_STROKE_RECT 0x00000002
#define THEME_RECIPE_OP_LINE 0x00000003
#define THEME_RECIPE_OP_GRADIENT_V 0x00000004
#define THEME_RECIPE_OP_GRADIENT_H 0x00000005
#define THEME_RECIPE_OP_GLYPH 0x00000006
#define THEME_RECIPE_OP_INSET_RECT 0x00000007

/***************************************************************************/
// Type definitions

typedef struct tag_THEME_RECIPE_STEP {
    U32 Operation;
    LPCSTR X1;
    LPCSTR Y1;
    LPCSTR X2;
    LPCSTR Y2;
    LPCSTR Color;
    LPCSTR Color1;
    LPCSTR Color2;
    U32 Thickness;
    LPCSTR Glyph;
    LPCSTR WhenState;
} THEME_RECIPE_STEP, *LPTHEME_RECIPE_STEP;

typedef struct tag_THEME_RECIPE {
    LPCSTR RecipeID;
    const THEME_RECIPE_STEP* Steps;
    U32 StepCount;
} THEME_RECIPE, *LPTHEME_RECIPE;

typedef struct tag_THEME_BINDING {
    LPCSTR ElementID;
    LPCSTR StateID;
    LPCSTR RecipeID;
} THEME_BINDING, *LPTHEME_BINDING;

/***************************************************************************/
// Other declarations

static const THEME_RECIPE_STEP RecipeWindowFrameClassicSteps[] = {
    {THEME_RECIPE_OP_STROKE_RECT, TEXT("0"), TEXT("0"), TEXT("w-1"), TEXT("h-1"), TEXT("0x00000000"), NULL, NULL, 1, NULL, NULL},
    {THEME_RECIPE_OP_STROKE_RECT, TEXT("1"), TEXT("1"), TEXT("w-2"), TEXT("h-2"), TEXT("0x00FFFFFF"), NULL, NULL, 1, NULL, NULL},
};

static const THEME_RECIPE Recipes[] = {
    {TEXT("window_frame_classic"), RecipeWindowFrameClassicSteps, sizeof(RecipeWindowFrameClassicSteps) / sizeof(RecipeWindowFrameClassicSteps[0])},
};

static const THEME_BINDING Bindings[] = {
    {TEXT("window.border"), TEXT("normal"), TEXT("window_frame_classic")},
    {TEXT("window.border"), TEXT("focused"), TEXT("window_frame_classic")},
    {TEXT("window.border"), TEXT("active"), TEXT("window_frame_classic")},
};

/***************************************************************************/

/**
 * @brief Compare start of one string with a prefix.
 * @param Text Full string.
 * @param Prefix Prefix to test.
 * @return TRUE when Text starts with Prefix.
 */
static BOOL ThemeStartsWith(LPCSTR Text, LPCSTR Prefix) {
    UINT Index;

    if (Text == NULL || Prefix == NULL) return FALSE;

    for (Index = 0; Prefix[Index] != STR_NULL; Index++) {
        if (Text[Index] != Prefix[Index]) return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Evaluate one recipe coordinate expression in element space.
 * @param Expression Coordinate expression.
 * @param Width Element width.
 * @param Height Element height.
 * @param Value Receives resolved coordinate.
 * @return TRUE on success.
 */
static BOOL ThemeRecipeResolveCoordinate(LPCSTR Expression, I32 Width, I32 Height, I32* Value) {
    LPCSTR Tail;
    I32 Base;
    I32 Delta;

    if (Expression == NULL || Value == NULL) return FALSE;
    if (Expression[0] == STR_NULL) return FALSE;

    if (Expression[0] == 'w' || Expression[0] == 'W') {
        Base = Width - 1;
        Tail = Expression + 1;
    } else if (Expression[0] == 'h' || Expression[0] == 'H') {
        Base = Height - 1;
        Tail = Expression + 1;
    } else {
        *Value = StringToI32(Expression);
        return TRUE;
    }

    if (Tail[0] == STR_NULL) {
        *Value = Base;
        return TRUE;
    }

    if (Tail[0] == '+') {
        Delta = StringToI32(Tail + 1);
        *Value = Base + Delta;
        return TRUE;
    }

    if (Tail[0] == '-') {
        Delta = StringToI32(Tail + 1);
        *Value = Base - Delta;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Clamp one value into [MinValue, MaxValue].
 * @param Value Value to clamp.
 * @param MinValue Lower bound.
 * @param MaxValue Upper bound.
 * @return Clamped value.
 */
static I32 ThemeClampI32(I32 Value, I32 MinValue, I32 MaxValue) {
    if (Value < MinValue) return MinValue;
    if (Value > MaxValue) return MaxValue;
    return Value;
}

/***************************************************************************/

/**
 * @brief Resolve a color literal or token reference.
 * @param ColorText Color text.
 * @param Color Receives resolved color.
 * @return TRUE on success.
 */
static BOOL ThemeRecipeResolveColor(LPCSTR ColorText, COLOR* Color) {
    STR HexBuffer[16];
    UINT Index;
    UINT Length;

    if (ColorText == NULL || Color == NULL) return FALSE;

    if (ThemeStartsWith(ColorText, TEXT("token:"))) {
        return DesktopThemeResolveTokenColorByName(ColorText + 6, Color);
    }

    if (ThemeStartsWith(ColorText, TEXT("0x")) || ThemeStartsWith(ColorText, TEXT("0X"))) {
        *Color = StringToU32(ColorText);
        return TRUE;
    }

    if (ColorText[0] != '#') return FALSE;

    Length = StringLength(ColorText);
    if (Length != 7 && Length != 9) return FALSE;

    HexBuffer[0] = '0';
    HexBuffer[1] = 'x';
    for (Index = 1; Index < Length; Index++) {
        HexBuffer[Index + 1] = ColorText[Index];
    }
    HexBuffer[Length + 1] = STR_NULL;
    *Color = StringToU32(HexBuffer);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Interpolate one ARGB color.
 * @param StartColor Start color.
 * @param EndColor End color.
 * @param Numerator Position numerator.
 * @param Denominator Position denominator.
 * @return Interpolated color.
 */
static COLOR ThemeInterpolateColor(COLOR StartColor, COLOR EndColor, U32 Numerator, U32 Denominator) {
    U32 Sa = (StartColor >> 24) & 0xFF;
    U32 Sr = (StartColor >> 16) & 0xFF;
    U32 Sg = (StartColor >> 8) & 0xFF;
    U32 Sb = StartColor & 0xFF;
    U32 Ea = (EndColor >> 24) & 0xFF;
    U32 Er = (EndColor >> 16) & 0xFF;
    U32 Eg = (EndColor >> 8) & 0xFF;
    U32 Eb = EndColor & 0xFF;
    U32 A;
    U32 R;
    U32 G;
    U32 B;

    if (Denominator == 0) return StartColor;

    A = Sa + (((I32)Ea - (I32)Sa) * Numerator) / Denominator;
    R = Sr + (((I32)Er - (I32)Sr) * Numerator) / Denominator;
    G = Sg + (((I32)Eg - (I32)Sg) * Numerator) / Denominator;
    B = Sb + (((I32)Eb - (I32)Sb) * Numerator) / Denominator;

    return ((A & 0xFF) << 24) | ((R & 0xFF) << 16) | ((G & 0xFF) << 8) | (B & 0xFF);
}

/***************************************************************************/

/**
 * @brief Draw one line with explicit color.
 * @param GC Graphics context.
 * @param X1 Start X.
 * @param Y1 Start Y.
 * @param X2 End X.
 * @param Y2 End Y.
 * @param Color Line color.
 * @return TRUE on success.
 */
static BOOL ThemeDrawColoredLine(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color) {
    PEN Pen;
    LINE_INFO LineInfo;
    HANDLE OldPen;

    if (GC == NULL) return FALSE;

    MemorySet(&Pen, 0, sizeof(PEN));
    Pen.TypeID = KOID_PEN;
    Pen.References = 1;
    Pen.Color = Color;
    Pen.Pattern = MAX_U32;

    LineInfo.Header.Size = sizeof(LINE_INFO);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GC;
    LineInfo.X1 = X1;
    LineInfo.Y1 = Y1;
    LineInfo.X2 = X2;
    LineInfo.Y2 = Y2;

    OldPen = SelectPen(GC, (HANDLE)&Pen);
    (void)Line(&LineInfo);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one filled rectangle with explicit color.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Fill color.
 * @return TRUE on success.
 */
static BOOL ThemeDrawFilledRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color) {
    BRUSH Brush;
    RECT_INFO RectInfo;
    HANDLE OldBrush;
    HANDLE OldPen;

    if (GC == NULL) return FALSE;

    MemorySet(&Brush, 0, sizeof(BRUSH));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    RectInfo.Header.Size = sizeof(RECT_INFO);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;
    RectInfo.CornerRadius = 0;
    RectInfo.CornerStyle = RECT_CORNER_STYLE_SQUARE;

    OldPen = SelectPen(GC, NULL);
    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    (void)KernelRectangle(&RectInfo);
    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one step rectangle in local coordinates.
 * @param Step Recipe step.
 * @param Rect Drawing rect.
 * @param OutX1 Receives X1.
 * @param OutY1 Receives Y1.
 * @param OutX2 Receives X2.
 * @param OutY2 Receives Y2.
 * @return TRUE on success.
 */
static BOOL ThemeResolveStepRect(const THEME_RECIPE_STEP* Step, LPRECT Rect, I32* OutX1, I32* OutY1, I32* OutX2, I32* OutY2) {
    I32 Width;
    I32 Height;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
    I32 Temp;

    if (Step == NULL || Rect == NULL || OutX1 == NULL || OutY1 == NULL || OutX2 == NULL || OutY2 == NULL) return FALSE;

    Width = Rect->X2 - Rect->X1 + 1;
    Height = Rect->Y2 - Rect->Y1 + 1;
    if (Width <= 0 || Height <= 0) return FALSE;

    if (ThemeRecipeResolveCoordinate(Step->X1, Width, Height, &X1) == FALSE) return FALSE;
    if (ThemeRecipeResolveCoordinate(Step->Y1, Width, Height, &Y1) == FALSE) return FALSE;
    if (ThemeRecipeResolveCoordinate(Step->X2, Width, Height, &X2) == FALSE) return FALSE;
    if (ThemeRecipeResolveCoordinate(Step->Y2, Width, Height, &Y2) == FALSE) return FALSE;

    if (X1 > X2) {
        Temp = X1;
        X1 = X2;
        X2 = Temp;
    }
    if (Y1 > Y2) {
        Temp = Y1;
        Y1 = Y2;
        Y2 = Temp;
    }

    X1 = ThemeClampI32(X1, 0, Width - 1);
    X2 = ThemeClampI32(X2, 0, Width - 1);
    Y1 = ThemeClampI32(Y1, 0, Height - 1);
    Y2 = ThemeClampI32(Y2, 0, Height - 1);

    *OutX1 = X1;
    *OutY1 = Y1;
    *OutX2 = X2;
    *OutY2 = Y2;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one stroke rectangle primitive.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Stroke color.
 * @param Thickness Border thickness.
 */
static void ThemeDrawStrokeRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color, U32 Thickness) {
    U32 Index;
    I32 Offset;

    if (Thickness == 0) Thickness = 1;

    for (Index = 0; Index < Thickness; Index++) {
        Offset = (I32)Index;
        if (X1 + Offset > X2 - Offset || Y1 + Offset > Y2 - Offset) break;

        (void)ThemeDrawColoredLine(GC, X1 + Offset, Y1 + Offset, X2 - Offset, Y1 + Offset, Color);
        (void)ThemeDrawColoredLine(GC, X2 - Offset, Y1 + Offset, X2 - Offset, Y2 - Offset, Color);
        (void)ThemeDrawColoredLine(GC, X2 - Offset, Y2 - Offset, X1 + Offset, Y2 - Offset, Color);
        (void)ThemeDrawColoredLine(GC, X1 + Offset, Y2 - Offset, X1 + Offset, Y1 + Offset, Color);
    }
}

/***************************************************************************/

/**
 * @brief Draw one recipe primitive step.
 * @param GC Graphics context.
 * @param Rect Drawing rect.
 * @param Step Step definition.
 * @param RequestedState Requested state identifier.
 * @return TRUE when step drew successfully.
 */
static BOOL ThemeDrawRecipeStep(HANDLE GC, LPRECT Rect, const THEME_RECIPE_STEP* Step, LPCSTR RequestedState) {
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
    COLOR Color;
    COLOR Color1;
    COLOR Color2;
    U32 Pos;
    U32 Span;

    if (GC == NULL || Rect == NULL || Step == NULL) return FALSE;

    if (Step->WhenState != NULL && RequestedState != NULL) {
        if (StringCompareNC(Step->WhenState, RequestedState) != 0) return TRUE;
    }

    if (ThemeResolveStepRect(Step, Rect, &X1, &Y1, &X2, &Y2) == FALSE) return FALSE;

    switch (Step->Operation) {
        case THEME_RECIPE_OP_FILL_RECT: {
            if (ThemeRecipeResolveColor(Step->Color ? Step->Color : Step->Color1, &Color) == FALSE) return FALSE;
            return ThemeDrawFilledRect(GC, X1, Y1, X2, Y2, Color);
        }

        case THEME_RECIPE_OP_STROKE_RECT: {
            if (ThemeRecipeResolveColor(Step->Color ? Step->Color : Step->Color1, &Color) == FALSE) return FALSE;
            ThemeDrawStrokeRect(GC, X1, Y1, X2, Y2, Color, Step->Thickness);
            return TRUE;
        }

        case THEME_RECIPE_OP_LINE: {
            if (ThemeRecipeResolveColor(Step->Color ? Step->Color : Step->Color1, &Color) == FALSE) return FALSE;
            return ThemeDrawColoredLine(GC, X1, Y1, X2, Y2, Color);
        }

        case THEME_RECIPE_OP_GRADIENT_V: {
            if (ThemeRecipeResolveColor(Step->Color1, &Color1) == FALSE) return FALSE;
            if (ThemeRecipeResolveColor(Step->Color2, &Color2) == FALSE) return FALSE;
            Span = (U32)(Y2 - Y1);
            for (Pos = 0; Pos <= Span; Pos++) {
                Color = ThemeInterpolateColor(Color1, Color2, Pos, Span);
                (void)ThemeDrawColoredLine(GC, X1, Y1 + (I32)Pos, X2, Y1 + (I32)Pos, Color);
            }
            return TRUE;
        }

        case THEME_RECIPE_OP_GRADIENT_H: {
            if (ThemeRecipeResolveColor(Step->Color1, &Color1) == FALSE) return FALSE;
            if (ThemeRecipeResolveColor(Step->Color2, &Color2) == FALSE) return FALSE;
            Span = (U32)(X2 - X1);
            for (Pos = 0; Pos <= Span; Pos++) {
                Color = ThemeInterpolateColor(Color1, Color2, Pos, Span);
                (void)ThemeDrawColoredLine(GC, X1 + (I32)Pos, Y1, X1 + (I32)Pos, Y2, Color);
            }
            return TRUE;
        }

        case THEME_RECIPE_OP_GLYPH: {
            if (ThemeRecipeResolveColor(Step->Color ? Step->Color : Step->Color1, &Color) == FALSE) return FALSE;
            if (Step->Glyph == NULL || Step->Glyph[0] == STR_NULL) return FALSE;

            if (Step->Glyph[0] == 'x' || Step->Glyph[0] == 'X') {
                (void)ThemeDrawColoredLine(GC, X1, Y1, X2, Y2, Color);
                (void)ThemeDrawColoredLine(GC, X2, Y1, X1, Y2, Color);
            } else if (Step->Glyph[0] == '-') {
                I32 MidY = (Y1 + Y2) / 2;
                (void)ThemeDrawColoredLine(GC, X1, MidY, X2, MidY, Color);
            } else {
                ThemeDrawStrokeRect(GC, X1, Y1, X2, Y2, Color, 1);
            }

            return TRUE;
        }

        case THEME_RECIPE_OP_INSET_RECT: {
            if (ThemeRecipeResolveColor(Step->Color1, &Color1) == FALSE) return FALSE;
            if (ThemeRecipeResolveColor(Step->Color2, &Color2) == FALSE) return FALSE;

            ThemeDrawStrokeRect(GC, X1, Y1, X2, Y2, Color1, Step->Thickness == 0 ? 1 : Step->Thickness);
            ThemeDrawStrokeRect(GC, X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, Color2, Step->Thickness == 0 ? 1 : Step->Thickness);
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Resolve one recipe identifier from element/state bindings.
 * @param ElementID Element identifier.
 * @param StateID Requested state identifier.
 * @return Recipe identifier or NULL.
 */
static LPCSTR ThemeResolveRecipeBinding(LPCSTR ElementID, LPCSTR StateID) {
    UINT Index;

    for (Index = 0; Index < (sizeof(Bindings) / sizeof(Bindings[0])); Index++) {
        if (StringCompareNC(Bindings[Index].ElementID, ElementID) != 0) continue;
        if (StringCompareNC(Bindings[Index].StateID, StateID) == 0) return Bindings[Index].RecipeID;
    }

    for (Index = 0; Index < (sizeof(Bindings) / sizeof(Bindings[0])); Index++) {
        if (StringCompareNC(Bindings[Index].ElementID, ElementID) != 0) continue;
        if (StringCompareNC(Bindings[Index].StateID, TEXT("normal")) == 0) return Bindings[Index].RecipeID;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Resolve one recipe by identifier.
 * @param RecipeID Recipe identifier.
 * @return Recipe pointer or NULL.
 */
static const THEME_RECIPE* ThemeFindRecipe(LPCSTR RecipeID) {
    UINT Index;

    if (RecipeID == NULL) return NULL;

    for (Index = 0; Index < (sizeof(Recipes) / sizeof(Recipes[0])); Index++) {
        if (StringCompareNC(Recipes[Index].RecipeID, RecipeID) == 0) return &Recipes[Index];
    }

    return NULL;
}

/***************************************************************************/

BOOL DesktopThemeDrawRecipeForElementState(HANDLE Window, HANDLE GC, LPRECT Rect, LPCSTR ElementID, LPCSTR StateID) {
    LPCSTR RecipeID;
    const THEME_RECIPE* Recipe;
    U32 StepIndex;
    U32 MaxPrimitives;

    UNUSED(Window);

    if (GC == NULL || Rect == NULL || ElementID == NULL || StateID == NULL) return FALSE;

    RecipeID = ThemeResolveRecipeBinding(ElementID, StateID);
    if (RecipeID == NULL) return FALSE;

    Recipe = ThemeFindRecipe(RecipeID);
    if (Recipe == NULL) return FALSE;

    MaxPrimitives = DESKTOP_THEME_MAX_PRIMITIVE_COUNT;
    if (Recipe->StepCount > MaxPrimitives) return FALSE;

    for (StepIndex = 0; StepIndex < Recipe->StepCount; StepIndex++) {
        if (ThemeDrawRecipeStep(GC, Rect, &Recipe->Steps[StepIndex], StateID) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/
