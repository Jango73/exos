/************************************************************************\

    EXOS Sample program - Terminal Tactics
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


    Terminal Tactics - Viewport Helpers

\************************************************************************/

#include "tt-map.h"
#include "tt-types.h"

/************************************************************************/

void MoveViewport(I32 deltaX, I32 deltaY) {
    if (App.GameState == NULL) return;

    if (App.GameState->MapWidth > 0) {
        App.GameState->ViewportPos.X = WrapCoord(App.GameState->ViewportPos.X, deltaX, App.GameState->MapWidth);
    }

    if (App.GameState->MapHeight > 0) {
        App.GameState->ViewportPos.Y = WrapCoord(App.GameState->ViewportPos.Y, deltaY, App.GameState->MapHeight);
    }
}

/************************************************************************/

BOOL GetScreenPosition(I32 objX, I32 objY, I32 width, I32 height, I32* screenX, I32* screenY) {
    I32 sx;
    I32 sy;
    I32 viewW;
    I32 viewH;

    if (App.GameState == NULL) return FALSE;
    if (App.GameState->MapWidth <= 0 || App.GameState->MapHeight <= 0) return FALSE;

    sx = objX - App.GameState->ViewportPos.X;
    sy = objY - App.GameState->ViewportPos.Y;

    if (sx < 0)
        sx += App.GameState->MapWidth;
    else if (sx >= App.GameState->MapWidth)
        sx -= App.GameState->MapWidth;

    if (sy < 0)
        sy += App.GameState->MapHeight;
    else if (sy >= App.GameState->MapHeight)
        sy -= App.GameState->MapHeight;

    viewW = (I32)VIEWPORT_WIDTH;
    viewH = (I32)VIEWPORT_HEIGHT;
    if (sx >= viewW || sy >= viewH) return FALSE;
    if (sx + width <= 0 || sy + height <= 0) return FALSE;

    if (screenX != NULL) *screenX = sx;
    if (screenY != NULL) *screenY = sy;
    return TRUE;
}
