
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


    VESA

\************************************************************************/

#include "GFX.h"
#include "Arch.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "log/Profile.h"
#include "drivers/graphics/vesa/VESA.h"
#include "utils/Graphics-Utils.h"
#include "utils/LineRasterizer.h"

/************************************************************************/

#define VESA_ENABLE_SELFTEST 1

/************************************************************************/

#define MKLINPTR(a) (((a & 0xFFFF0000) >> 12) + (a & 0x0000FFFF))

#define CLIPVALUE(val, min, max) \
    {                            \
        if (val < min)           \
            val = min;           \
        else if (val > max)      \
            val = max;           \
    }

/************************************************************************/

typedef struct tag_VESAINFOBLOCK {
    U8 Signature[4];  // 4 signature bytes
    U16 Version;      // VESA version number
    U32 OEMString;    // Pointer to OEM string
    U8 Caps[4];       // Capabilities of the video environment
    U32 ModePointer;  // Pointer to supported Super VGA modes
    U16 Memory;       // Number of 64kb memory blocks on board
} VESAINFOBLOCK, *LPVESAINFOBLOCK;

/************************************************************************/

typedef struct tag_MODEINFOBLOCK {
    U16 Attributes;
    U8 WindowAAttributes;
    U8 WindowBAttributes;
    U16 WindowGranularity;
    U16 WindowSize;
    U16 WindowAStartSegment;
    U16 WindowBStartSegment;
    U32 WindowFunctionPointer;
    U16 BytesPerScanLine;

    U16 XResolution;
    U16 YResolution;
    U8 XCharSize;
    U8 YCharSize;
    U8 NumberOfPlanes;
    U8 BitsPerPixel;
    U8 NumberOfBanks;
    U8 MemoryModel;
    U8 BankSizeKB;
    U8 NumberOfImagePages;
    U8 Reserved;

    U8 RedMaskSize;
    U8 RedFieldPosition;
    U8 GreenMaskSize;
    U8 GreenFieldPosition;
    U8 BlueMaskSize;
    U8 BlueFieldPosition;
    U8 RsvdMaskSize;
    U8 RsvdFieldPosition;
    U8 DirectColorModeInfo;
    U32 PhysBasePtr;
    U32 OffScreenMemOffset;
    U16 OffScreenMemSize;
    U8 Reserved2[206];
} MODEINFOBLOCK, *LPMODEINFOBLOCK;

/************************************************************************/

typedef struct tag_VIDEOMODESPECS {
    U32 Mode;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    COLOR (*SetPixel)(LPVESA_CONTEXT, I32, I32, COLOR);
    COLOR (*GetPixel)(LPVESA_CONTEXT, I32, I32);
    U32 (*Line)(LPVESA_CONTEXT, I32, I32, I32, I32);
    U32 (*Rect)(LPVESA_CONTEXT, I32, I32, I32, I32);
} VIDEOMODESPECS, *LPVIDEOMODESPECS;

/************************************************************************/

struct tag_VESA_CONTEXT {
    GRAPHICSCONTEXT Header;
    VESAINFOBLOCK VESAInfo;
    MODEINFOBLOCK ModeInfo;
    VIDEOMODESPECS ModeSpecs;
    U32 PixelSize;
    PHYSICAL FrameBufferPhysical;
    LINEAR FrameBufferLinear;
    U32 FrameBufferSize;
    BOOL LinearFrameBufferEnabled;
};

static BOOL VESAPlotLinePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color);

COLOR SetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Plane = Context->Header.MemoryBase + Offset;
    OldColor = *Plane;

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            *Plane = (U8)Color;
        } break;

        case ROP_XOR: {
            *Plane ^= (U8)Color;
        } break;

        case ROP_OR: {
            *Plane |= (U8)Color;
        } break;

        case ROP_AND: {
            *Plane &= (U8)Color;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Write a 16bpp pixel using current raster operation.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @param Color 16-bit color value
 * @return Previous pixel value
 */
COLOR SetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Plane = Context->Header.MemoryBase + Offset;
    OldColor = *((U16*)Plane);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            *((U16*)Plane) = (U16)Color;
        } break;

        case ROP_XOR: {
            *((U16*)Plane) ^= (U16)Color;
        } break;

        case ROP_OR: {
            *((U16*)Plane) |= (U16)Color;
        } break;

        case ROP_AND: {
            *((U16*)Plane) &= (U16)Color;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Write a 24bpp pixel using current raster operation.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @param Color 24-bit packed RGB color
 * @return Previous pixel value
 */
COLOR SetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Pixel;
    U8 R;
    U8 G;
    U8 B;
    U32 Converted;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X * 3);
    Pixel = Context->Header.MemoryBase + Offset;

    Converted = 0;
    Converted |= (((Color >> 0) & 0xFF) << 16);
    Converted |= (((Color >> 8) & 0xFF) << 8);
    Converted |= (((Color >> 16) & 0xFF) << 0);

    R = (U8)((Converted >> 0) & 0xFF);
    G = (U8)((Converted >> 8) & 0xFF);
    B = (U8)((Converted >> 16) & 0xFF);

    OldColor = (U32)Pixel[0] | ((U32)Pixel[1] << 8) | ((U32)Pixel[2] << 16);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            Pixel[0] = R;
            Pixel[1] = G;
            Pixel[2] = B;
        } break;

        case ROP_XOR: {
            Pixel[0] ^= R;
            Pixel[1] ^= G;
            Pixel[2] ^= B;
        } break;

        case ROP_OR: {
            Pixel[0] |= R;
            Pixel[1] |= G;
            Pixel[2] |= B;
        } break;

        case ROP_AND: {
            Pixel[0] &= R;
            Pixel[1] &= G;
            Pixel[2] &= B;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Read an 8bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 8-bit color value or 0 if out of clip
 */
COLOR GetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Color = *((U8*)(Context->Header.MemoryBase + Offset));

    return Color;
}

/***************************************************************************/

/**
 * @brief Read a 16bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 16-bit color value or 0 if out of clip
 */
COLOR GetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Color = *((U16*)(Context->Header.MemoryBase + Offset));

    return Color;
}

/***************************************************************************/

/**
 * @brief Read a 24bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 24-bit packed RGB color or 0 if out of clip
 */
COLOR GetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U8* Pixel;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + (X * 3);
    Pixel = Context->Header.MemoryBase + Offset;

    Color = (U32)Pixel[0];
    Color |= (U32)Pixel[1] << 8;
    Color |= (U32)Pixel[2] << 16;

    return Color;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 8bpp mode (stub).
 *
 * Parameters are unused; kept for interface parity.
 *
 * @return Always 0
 */
U32 Line8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    if (Context == NULL || Context->Header.Pen == NULL || Context->Header.Pen->TypeID != KOID_PEN) return MAX_U32;
    LineRasterizerDraw(
        Context,
        X1,
        Y1,
        X2,
        Y2,
        Context->Header.Pen->Color,
        Context->Header.Pen->Pattern,
        Context->Header.Pen->Width != 0 ? Context->Header.Pen->Width : 1,
        VESAPlotLinePixel);
    return 0;
}

/***************************************************************************/

static BOOL VESAPlotLinePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color) {
    LPVESA_CONTEXT VesaContext = (LPVESA_CONTEXT)Context;

    if (VesaContext == NULL || Color == NULL) return FALSE;
    VesaContext->ModeSpecs.SetPixel(VesaContext, X, Y, *Color);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 16bpp mode.
 *
 * @param Context VESA context with pen state
 * @param X1 Start X
 * @param Y1 Start Y
 * @param X2 End X
 * @param Y2 End Y
 * @return 0 on success, MAX_U32 on invalid pen
 */
U32 Line16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    if (Context == NULL || Context->Header.Pen == NULL || Context->Header.Pen->TypeID != KOID_PEN) return MAX_U32;
    LineRasterizerDraw(
        Context,
        X1,
        Y1,
        X2,
        Y2,
        Context->Header.Pen->Color,
        Context->Header.Pen->Pattern,
        Context->Header.Pen->Width != 0 ? Context->Header.Pen->Width : 1,
        VESAPlotLinePixel);
    return 0;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 24bpp mode.
 *
 * @param Context VESA context with pen state
 * @param X1 Start X
 * @param Y1 Start Y
 * @param X2 End X
 * @param Y2 End Y
 * @return 0 on success, MAX_U32 on invalid pen
 */
U32 Line24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    if (Context == NULL || Context->Header.Pen == NULL || Context->Header.Pen->TypeID != KOID_PEN) return MAX_U32;
    LineRasterizerDraw(
        Context,
        X1,
        Y1,
        X2,
        Y2,
        Context->Header.Pen->Color,
        Context->Header.Pen->Pattern,
        Context->Header.Pen->Width != 0 ? Context->Header.Pen->Width : 1,
        VESAPlotLinePixel);
    return 0;
}

/***************************************************************************/

/**
 * @brief Fill rectangle in 8bpp mode (stub).
 *
 * Parameters are unused; kept for interface parity.
 *
 * @return Always 0
 */
U32 Rect8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    UNUSED(Context);
    UNUSED(X1);
    UNUSED(Y1);
    UNUSED(X2);
    UNUSED(Y2);
    /*
      long  offset;
      long  bank;
      word  x, y;
      word  tmp;
      word  line_bit=0;
      ubyte *plane;

      if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
      if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

      if (mode == _GBORDER)
      {
    _moveto(x1, y1);
    _lineto(x2, y1);
    _lineto(x2, y2);
    _lineto(x1, y2);
    _lineto(x1, y1);
      }
      else
      if (mode == _GFILLINTERIOR)
      {
    if (x1 < Context->Header.LoClip.X) x1 = Context->Header.LoClip.X;
    else
    if (x1 > Context->Header.HiClip.X) x1 = Context->Header.HiClip.X;
    if (x2 < Context->Header.LoClip.X) x2 = Context->Header.LoClip.X;
    else
    if (x2 > Context->Header.HiClip.X) x2 = Context->Header.HiClip.X;
    if (y1 < Context->Header.LoClip.Y) y1 = Context->Header.LoClip.Y;
    else
    if (y1 > Context->Header.HiClip.Y) y1 = Context->Header.HiClip.Y;
    if (y2 < Context->Header.LoClip.Y) y2 = Context->Header.LoClip.Y;
    else
    if (y2 > Context->Header.HiClip.Y) y2 = Context->Header.HiClip.Y;

    offset = (y1 * Context->Header.BytesPerScanLine) + x1;
    bank   = offset >> Context->GranularShift;

    switch_bank();

    for (y = y1; y <= y2; y++)
    {
      plane = Context->Header.MemoryBase + (offset &
      Context->GranularModulo);

      for (x = x1; x <= x2; x++)
      {
        switch (Context->Header.RasterOperation)
        {
          case _GPSET :
          {
        *plane = (U8) Context->CurrentColor;
          }
          break;
          case _GXOR :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor ^ (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
          case _GOR :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor | (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
          case _GAND :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor & (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
        }
        plane++;
      }

      offset += Context->Header.BytesPerScanLine;
      bank = offset >> Context->GranularShift;
      switch_bank();
    }
      }
    */

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw filled and/or outlined rectangle in 16bpp mode.
 *
 * Uses brush for fill and pen for border when provided.
 *
 * @param Context VESA context
 * @param X1 Left coordinate
 * @param Y1 Top coordinate
 * @param X2 Right coordinate
 * @param Y2 Bottom coordinate
 * @return 0 on completion
 */
U32 Rect16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    U32 Temp;
    U32 Color;
    RECT SourceRect;
    RECT ClipRect;
    RECT DrawRect;
    PROFILE_SCOPE Scope;

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

    SourceRect.X1 = X1;
    SourceRect.Y1 = Y1;
    SourceRect.X2 = X2;
    SourceRect.Y2 = Y2;
    ClipRect.X1 = Context->Header.LoClip.X;
    ClipRect.Y1 = Context->Header.LoClip.Y;
    ClipRect.X2 = Context->Header.HiClip.X;
    ClipRect.Y2 = Context->Header.HiClip.Y;
    if (IntersectRect(&SourceRect, &ClipRect, &DrawRect) == FALSE) {
        return 0;
    }

    if (Context->Header.Brush != NULL && Context->Header.Brush->TypeID == KOID_BRUSH) {
        ProfileStart(&Scope, TEXT("VESA.Rect16Fill"));
        Color = Context->Header.Brush->Color;
        (void)GraphicsFillSolidRect((LPGRAPHICSCONTEXT)&(Context->Header), DrawRect.X1, DrawRect.Y1, DrawRect.X2, DrawRect.Y2, Color);
        ProfileStop(&Scope);
    }

    if (Context->Header.Pen != NULL && Context->Header.Pen->TypeID == KOID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw filled and/or outlined rectangle in 24bpp mode.
 *
 * Uses brush for fill and pen for border when provided.
 *
 * @param Context VESA context
 * @param X1 Left coordinate
 * @param Y1 Top coordinate
 * @param X2 Right coordinate
 * @param Y2 Bottom coordinate
 * @return 0 on completion
 */
U32 Rect24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    U32 Temp;
    RECT SourceRect;
    RECT ClipRect;
    RECT DrawRect;
    PROFILE_SCOPE Scope;

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

    SourceRect.X1 = X1;
    SourceRect.Y1 = Y1;
    SourceRect.X2 = X2;
    SourceRect.Y2 = Y2;
    ClipRect.X1 = Context->Header.LoClip.X;
    ClipRect.Y1 = Context->Header.LoClip.Y;
    ClipRect.X2 = Context->Header.HiClip.X;
    ClipRect.Y2 = Context->Header.HiClip.Y;

    if (IntersectRect(&SourceRect, &ClipRect, &DrawRect) == FALSE) {
        return 0;
    }

    if (Context->Header.Brush != NULL && Context->Header.Brush->TypeID == KOID_BRUSH) {
        ProfileStart(&Scope, TEXT("VESA.Rect24Fill"));
        (void)GraphicsFillSolidRect(
            (LPGRAPHICSCONTEXT)&(Context->Header),
            DrawRect.X1,
            DrawRect.Y1,
            DrawRect.X2,
            DrawRect.Y2,
            Context->Header.Brush->Color
        );
        ProfileStop(&Scope);
    }

    // Draw borders

    if (Context->Header.Pen != NULL && Context->Header.Pen->TypeID == KOID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

U32 VESATrianglePrimitive(LPVESA_CONTEXT Context, LPTRIANGLE_INFO Info) {
    if (Context == NULL || Info == NULL) return 0;
    (void)GraphicsDrawTriangleFromDescriptor((LPGRAPHICSCONTEXT)&(Context->Header), Info);
    return 1;
}

/***************************************************************************/

U32 VESAArcPrimitive(LPVESA_CONTEXT Context, LPARC_INFO Info) {
    if (Context == NULL || Info == NULL) return 0;
    (void)GraphicsDrawArcFromDescriptor((LPGRAPHICSCONTEXT)&(Context->Header), Info);
    return 1;
}

/***************************************************************************/

/**
 * @brief Draw a simple self-test pattern for sanity checks.
 *
 * Renders colored horizontal bands in the top portion of the frame buffer.
 *
 * @param Context VESA context
 */
void VESADrawSelfTest(LPVESA_CONTEXT Context) {
    static const COLOR Colors[] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00};
    const I32 NumBands = (I32)(sizeof(Colors) / sizeof(Colors[0]));
    COLOR (*SetPixel)(LPVESA_CONTEXT, I32, I32, COLOR);
    I32 Width;
    I32 Height;
    I32 StripeWidth;
    I32 TestHeight;
    I32 Index;
    I32 X;
    I32 Y;
    I32 X1;
    I32 X2;

    SetPixel = Context->ModeSpecs.SetPixel;
    if (SetPixel == NULL) return;

    Width = (I32)Context->Header.Width;
    Height = (I32)Context->Header.Height;
    if (Width <= 0 || Height <= 0) return;

    StripeWidth = Width / NumBands;
    if (StripeWidth <= 0) StripeWidth = Width;

    TestHeight = Height / 16;
    if (TestHeight < 16) TestHeight = Height;
    if (TestHeight > Height) TestHeight = Height;

    DEBUG(TEXT("Drawing %u color bands (%ux%u test area)"), (U32)NumBands, (U32)Width, (U32)TestHeight);

    for (Index = 0; Index < NumBands; Index++) {
        X1 = Index * StripeWidth;
        X2 = X1 + StripeWidth - 1;

        if (Index == NumBands - 1) X2 = Width - 1;
        if (X2 >= Width) X2 = Width - 1;
        if (X1 < 0) X1 = 0;

        for (Y = 0; Y < TestHeight; Y++) {
            for (X = X1; X <= X2; X++) {
                SetPixel(Context, X, Y, Colors[Index]);
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Create a brush object from descriptor.
 *
 * @param Info Brush creation parameters
 * @return Allocated brush or NULL on failure
 */
