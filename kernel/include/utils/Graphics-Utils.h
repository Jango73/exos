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


    Graphics utility helpers

\************************************************************************/

#ifndef GRAPHICS_UTILS_H_INCLUDED
#define GRAPHICS_UTILS_H_INCLUDED

/************************************************************************/

#include "GFX.h"
#include "utils/RectRegion.h"

/************************************************************************/

typedef BOOL (*GRAPHICS_PLOT_PIXEL_ROUTINE)(LPVOID Context, I32 X, I32 Y, COLOR* Color);

/************************************************************************/

BOOL IntersectRect(LPRECT Left, LPRECT Right, LPRECT Result);
BOOL SubtractRectFromRect(LPRECT Source, LPRECT Occluder, LPRECT_REGION Region);
BOOL SubtractRectFromRegion(LPRECT_REGION Region, LPRECT Occluder, LPRECT TempStorage, UINT TempCapacity);
void GraphicsResolveChannelLayout(
    LPGRAPHICSCONTEXT Context,
    U32* RedPositionOut,
    U32* RedMaskSizeOut,
    U32* GreenPositionOut,
    U32* GreenMaskSizeOut,
    U32* BluePositionOut,
    U32* BlueMaskSizeOut);
U32 GraphicsPackColor(LPGRAPHICSCONTEXT Context, COLOR Color);
COLOR GraphicsUnpackColor(LPGRAPHICSCONTEXT Context, U32 PackedColor);
BOOL GraphicsWritePixel(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR Color);
BOOL GraphicsReadPixel(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR* ColorOut);
BOOL GraphicsDrawScanline(LPGRAPHICSCONTEXT Context, I32 X1, I32 X2, I32 Y, COLOR StartColor, COLOR EndColor);
BOOL GraphicsFillSolidRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR FillColor);
BOOL GraphicsFillVerticalGradientRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor);
BOOL GraphicsFillHorizontalGradientRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor);
BOOL GraphicsFillRectangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info);
BOOL GraphicsDrawRectangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info);
I32 GraphicsTriangleEdgeFunction(I32 Ax, I32 Ay, I32 Bx, I32 By, I32 Px, I32 Py);
BOOL GraphicsFillTriangleSpans(LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR FillColor, LPRECT FilledBounds);
BOOL GraphicsFillTriangleVerticalGradient(
    LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR StartColor, COLOR EndColor, LPRECT FilledBounds);
BOOL GraphicsFillTriangleHorizontalGradient(
    LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR StartColor, COLOR EndColor, LPRECT FilledBounds);
BOOL GraphicsDrawTriangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info);
BOOL GraphicsStrokeArc(LPVOID Context, GRAPHICS_PLOT_PIXEL_ROUTINE PlotPixel, I32 CenterX, I32 CenterY, I32 Radius, COLOR StrokeColor);
BOOL GraphicsDrawArcFromDescriptor(LPGRAPHICSCONTEXT Context, LPARC_INFO Info);
BOOL GraphicsDrawTestPattern(LPGRAPHICSCONTEXT Context);

// Coordinate spaces:
// - Screen: absolute desktop pixels.
// - Window: pixels relative to the full window rectangle (frame included).
// - Client: pixels relative to the client rectangle origin.
void GraphicsScreenRectToWindowRect(LPRECT WindowScreenRect, LPRECT ScreenRect, LPRECT WindowRect);
void GraphicsWindowRectToScreenRect(LPRECT WindowScreenRect, LPRECT WindowRect, LPRECT ScreenRect);
void GraphicsScreenPointToWindowPoint(LPRECT WindowScreenRect, LPPOINT ScreenPoint, LPPOINT WindowPoint);
void GraphicsWindowPointToScreenPoint(LPRECT WindowScreenRect, LPPOINT WindowPoint, LPPOINT ScreenPoint);

/************************************************************************/

#endif  // GRAPHICS_UTILS_H_INCLUDED
