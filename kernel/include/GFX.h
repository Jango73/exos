
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


    GFX

\************************************************************************/

#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "process/Process.h"
#include "User.h"

/***************************************************************************/

// Functions supplied by a graphics driver

#define DF_GFX_GETMODECOUNT (DF_FIRST_FUNCTION + 0)
#define DF_GFX_GETMODEINFO (DF_FIRST_FUNCTION + 1)
#define DF_GFX_SETMODE (DF_FIRST_FUNCTION + 2)
#define DF_GFX_GETCONTEXT (DF_FIRST_FUNCTION + 3)
#define DF_GFX_CREATEBRUSH (DF_FIRST_FUNCTION + 4)
#define DF_GFX_CREATEPEN (DF_FIRST_FUNCTION + 5)
#define DF_GFX_SETPIXEL (DF_FIRST_FUNCTION + 6)
#define DF_GFX_GETPIXEL (DF_FIRST_FUNCTION + 7)
#define DF_GFX_LINE (DF_FIRST_FUNCTION + 8)
#define DF_GFX_RECTANGLE (DF_FIRST_FUNCTION + 9)
#define DF_GFX_ELLIPSE (DF_FIRST_FUNCTION + 10)
#define DF_GFX_GETCAPABILITIES (DF_FIRST_FUNCTION + 11)
#define DF_GFX_ENUMOUTPUTS (DF_FIRST_FUNCTION + 12)
#define DF_GFX_GETOUTPUTINFO (DF_FIRST_FUNCTION + 13)
#define DF_GFX_PRESENT (DF_FIRST_FUNCTION + 14)
#define DF_GFX_WAITVBLANK (DF_FIRST_FUNCTION + 15)
#define DF_GFX_ALLOCSURFACE (DF_FIRST_FUNCTION + 16)
#define DF_GFX_FREESURFACE (DF_FIRST_FUNCTION + 17)
#define DF_GFX_SETSCANOUT (DF_FIRST_FUNCTION + 18)
#define DF_GFX_TEXT_PUTCELL (DF_FIRST_FUNCTION + 19)
#define DF_GFX_TEXT_CLEAR_REGION (DF_FIRST_FUNCTION + 20)
#define DF_GFX_TEXT_SCROLL_REGION (DF_FIRST_FUNCTION + 21)
#define DF_GFX_TEXT_SET_CURSOR (DF_FIRST_FUNCTION + 22)
#define DF_GFX_TEXT_SET_CURSOR_VISIBLE (DF_FIRST_FUNCTION + 23)
#define DF_GFX_CURSOR_SET_SHAPE (DF_FIRST_FUNCTION + 24)
#define DF_GFX_CURSOR_SET_POSITION (DF_FIRST_FUNCTION + 25)
#define DF_GFX_CURSOR_SET_VISIBLE (DF_FIRST_FUNCTION + 26)
#define DF_GFX_ARC (DF_FIRST_FUNCTION + 27)
#define DF_GFX_TRIANGLE (DF_FIRST_FUNCTION + 28)
#define DF_GFX_TEXT_DRAW (DF_FIRST_FUNCTION + 29)
#define DF_GFX_TEXT_MEASURE (DF_FIRST_FUNCTION + 30)

/***************************************************************************/

// Graphics driver error codes

#define DF_GFX_ERROR_MODEUNAVAIL DF_RETURN_FIRST

/***************************************************************************/

typedef U32 (*GFXGETMODECOUNTFUNC)(void);

#define ROP_SET 0x0001
#define ROP_AND 0x0002
#define ROP_OR 0x0003
#define ROP_XOR 0x0004

/***************************************************************************/

// Graphics output types

#define GFX_OUTPUT_TYPE_UNKNOWN 0x0000
#define GFX_OUTPUT_TYPE_EDP 0x0001
#define GFX_OUTPUT_TYPE_HDMI 0x0002
#define GFX_OUTPUT_TYPE_DISPLAYPORT 0x0003
#define GFX_OUTPUT_TYPE_VGA 0x0004

/***************************************************************************/

// Graphics pixel formats

#define GFX_FORMAT_UNKNOWN 0x0000
#define GFX_FORMAT_XRGB8888 0x0001
#define GFX_FORMAT_ARGB8888 0x0002
#define GFX_FORMAT_RGB565 0x0003
#define GFX_FORMAT_RGB888 0x0004

/***************************************************************************/

// Surface and present flags

#define GFX_SURFACE_FLAG_SCANOUT 0x0001
#define GFX_SURFACE_FLAG_CPU_VISIBLE 0x0002
#define GFX_PRESENT_FLAG_WAIT_VBLANK 0x0001

/***************************************************************************/

// Cursor formats

#define GFX_CURSOR_FORMAT_ARGB8888 0x0001

/***************************************************************************/

#define GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY 0x00000001

/***************************************************************************/

typedef struct tag_BRUSH {
    LISTNODE_FIELDS
    U32 Color;
    U32 Pattern;
} BRUSH, *LPBRUSH;

/***************************************************************************/

typedef struct tag_PEN {
    LISTNODE_FIELDS
    U32 Color;
    U32 Pattern;
    U32 Width;
} PEN, *LPPEN;

/***************************************************************************/

typedef struct tag_FONT {
    LISTNODE_FIELDS
} FONT, *LPFONT;

/***************************************************************************/

struct tag_FONT_FACE;

/***************************************************************************/

typedef struct tag_BITMAP {
    LISTNODE_FIELDS
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    U32 BytesPerScanLine;
    U8* Data;
} BITMAP, *LPBITMAP;

/***************************************************************************/

// The graphics context

typedef struct tag_GRAPHICSCONTEXT {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPDRIVER Driver;
    U32 Flags;
    I32 Width;
    I32 Height;
    U32 BitsPerPixel;
    U32 BytesPerScanLine;
    U32 RedPosition;
    U32 RedMaskSize;
    U32 GreenPosition;
    U32 GreenMaskSize;
    U32 BluePosition;
    U32 BlueMaskSize;
    U8* MemoryBase;
    POINT LoClip;
    POINT HiClip;
    POINT Origin;
    U32 RasterOperation;
    LPBRUSH Brush;
    LPPEN Pen;
    LPFONT Font;
    LPBITMAP Bitmap;
} GRAPHICS_CONTEXT, *LPGRAPHICSCONTEXT;

/***************************************************************************/

typedef struct tag_GFX_CAPABILITIES {
    ABI_HEADER Header;
    BOOL HasHardwareModeset;
    BOOL HasPageFlip;
    BOOL HasVBlankInterrupt;
    BOOL HasCursorPlane;
    BOOL SupportsTiledSurface;
    U32 MaxWidth;
    U32 MaxHeight;
    U32 PreferredFormat;
} GFX_CAPABILITIES, *LPGFX_CAPABILITIES;

/***************************************************************************/

typedef struct tag_GFX_OUTPUT_QUERY {
    ABI_HEADER Header;
    U32 Index;
    U32 OutputId;
} GFX_OUTPUT_QUERY, *LPGFX_OUTPUT_QUERY;

/***************************************************************************/

typedef struct tag_GFX_OUTPUT_INFO {
    ABI_HEADER Header;
    U32 OutputId;
    U32 Type;
    BOOL IsConnected;
    U32 NativeWidth;
    U32 NativeHeight;
    U32 RefreshRate;
} GFX_OUTPUT_INFO, *LPGFX_OUTPUT_INFO;

/***************************************************************************/

typedef struct tag_GFX_SURFACE_INFO {
    ABI_HEADER Header;
    U32 SurfaceId;
    U32 Width;
    U32 Height;
    U32 Format;
    U32 Pitch;
    U8* MemoryBase;
    U32 Flags;
} GFX_SURFACE_INFO, *LPGFX_SURFACE_INFO;

/***************************************************************************/

typedef struct tag_GFX_PRESENT_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    U32 SurfaceId;
    RECT DirtyRect;
    U32 Flags;
} GFX_PRESENT_INFO, *LPGFX_PRESENT_INFO;

/***************************************************************************/

typedef struct tag_GFX_VBLANK_INFO {
    ABI_HEADER Header;
    U32 TimeoutMilliseconds;
    U32 FrameSequence;
} GFX_VBLANK_INFO, *LPGFX_VBLANK_INFO;

/***************************************************************************/

typedef struct tag_GFX_SCANOUT_INFO {
    ABI_HEADER Header;
    U32 OutputId;
    U32 SurfaceId;
    U32 Width;
    U32 Height;
    U32 Format;
    U32 Flags;
} GFX_SCANOUT_INFO, *LPGFX_SCANOUT_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_CELL_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    U32 CellX;
    U32 CellY;
    U32 CellWidth;
    U32 CellHeight;
    STR Character;
    U32 ForegroundColorIndex;
    U32 BackgroundColorIndex;
} GFX_TEXT_CELL_INFO, *LPGFX_TEXT_CELL_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_REGION_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    U32 CellX;
    U32 CellY;
    U32 RegionCellWidth;
    U32 RegionCellHeight;
    U32 GlyphCellWidth;
    U32 GlyphCellHeight;
    U32 ForegroundColorIndex;
    U32 BackgroundColorIndex;
} GFX_TEXT_REGION_INFO, *LPGFX_TEXT_REGION_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_CURSOR_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    U32 CellX;
    U32 CellY;
    U32 CellWidth;
    U32 CellHeight;
    U32 ForegroundColorIndex;
} GFX_TEXT_CURSOR_INFO, *LPGFX_TEXT_CURSOR_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_CURSOR_VISIBLE_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    BOOL IsVisible;
} GFX_TEXT_CURSOR_VISIBLE_INFO, *LPGFX_TEXT_CURSOR_VISIBLE_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_DRAW_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X;
    I32 Y;
    LPCSTR Text;
    const struct tag_FONT_FACE* Font;
} GFX_TEXT_DRAW_INFO, *LPGFX_TEXT_DRAW_INFO;

/***************************************************************************/

typedef struct tag_GFX_TEXT_MEASURE_INFO {
    ABI_HEADER Header;
    LPCSTR Text;
    const struct tag_FONT_FACE* Font;
    U32 Width;
    U32 Height;
} GFX_TEXT_MEASURE_INFO, *LPGFX_TEXT_MEASURE_INFO;

/***************************************************************************/

typedef struct tag_GFX_CURSOR_SHAPE_INFO {
    ABI_HEADER Header;
    U32 Width;
    U32 Height;
    U32 HotspotX;
    U32 HotspotY;
    U32 Format;
    U32 Pitch;
    LPCVOID Pixels;
} GFX_CURSOR_SHAPE_INFO, *LPGFX_CURSOR_SHAPE_INFO;

/***************************************************************************/

typedef struct tag_GFX_CURSOR_POSITION_INFO {
    ABI_HEADER Header;
    I32 X;
    I32 Y;
} GFX_CURSOR_POSITION_INFO, *LPGFX_CURSOR_POSITION_INFO;

/***************************************************************************/

typedef struct tag_GFX_CURSOR_VISIBLE_INFO {
    ABI_HEADER Header;
    BOOL IsVisible;
} GFX_CURSOR_VISIBLE_INFO, *LPGFX_CURSOR_VISIBLE_INFO;

/***************************************************************************/

#endif  // GFX_H_INCLUDED
