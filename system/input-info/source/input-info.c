
/************************************************************************\

    EXOS Sample program
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


    Input Info - Display mouse and media-key state in real time

\************************************************************************/

#include "../../../runtime/include/exos-runtime.h"
#include "../../../runtime/include/exos.h"

/************************************************************************/

/**
 * @brief Media key virtual key values.
 */
#define VK_MEDIA_PLAY 0x90
#define VK_MEDIA_PAUSE 0x91
#define VK_MEDIA_PLAY_PAUSE 0x92
#define VK_MEDIA_STOP 0x93
#define VK_MEDIA_NEXT 0x94
#define VK_MEDIA_PREV 0x95
#define VK_MEDIA_MUTE 0x96
#define VK_MEDIA_VOLUME_UP 0x97
#define VK_MEDIA_VOLUME_DOWN 0x98
#define VK_MEDIA_BRIGHTNESS_UP 0x99
#define VK_MEDIA_BRIGHTNESS_DOWN 0x9A
#define VK_MEDIA_SLEEP 0x9B
#define VK_MEDIA_EJECT 0x9C
#define VK_CUT 0x9D
#define VK_COPY 0x9E
#define VK_PASTE 0x9F

/************************************************************************/

typedef struct tag_MEDIA_INDICATOR {
    U32 VirtualKey;
    const char* Name;
    U32 Pressed;
    U32 PressCount;
} MEDIA_INDICATOR, *LPMEDIA_INDICATOR;

/************************************************************************/

static MEDIA_INDICATOR MediaIndicators[] = {
    {VK_MEDIA_PLAY, "PLAY", 0, 0},
    {VK_MEDIA_PAUSE, "PAUSE", 0, 0},
    {VK_MEDIA_PLAY_PAUSE, "PLAY/PAUSE", 0, 0},
    {VK_MEDIA_STOP, "STOP", 0, 0},
    {VK_MEDIA_NEXT, "NEXT", 0, 0},
    {VK_MEDIA_PREV, "PREV", 0, 0},
    {VK_MEDIA_MUTE, "MUTE", 0, 0},
    {VK_MEDIA_VOLUME_UP, "VOL+", 0, 0},
    {VK_MEDIA_VOLUME_DOWN, "VOL-", 0, 0},
    {VK_MEDIA_BRIGHTNESS_UP, "BRT+", 0, 0},
    {VK_MEDIA_BRIGHTNESS_DOWN, "BRT-", 0, 0},
    {VK_MEDIA_SLEEP, "SLEEP", 0, 0},
    {VK_MEDIA_EJECT, "EJECT", 0, 0},
    {VK_CUT, "CUT", 0, 0},
    {VK_COPY, "COPY", 0, 0},
    {VK_PASTE, "PASTE", 0, 0},
};

/************************************************************************/

/**
 * @brief Update one media indicator from a keyboard message.
 *
 * @param VirtualKey Virtual key code from key message.
 * @param Pressed Non-zero when key is pressed.
 * @return TRUE when a media indicator was updated.
 */
static BOOL UpdateMediaIndicator(U32 VirtualKey, BOOL Pressed) {
    U32 Index = 0;

    for (Index = 0; Index < (sizeof(MediaIndicators) / sizeof(MediaIndicators[0])); Index++) {
        if (MediaIndicators[Index].VirtualKey == VirtualKey) {
            MediaIndicators[Index].Pressed = Pressed ? 1 : 0;
            if (Pressed) {
                MediaIndicators[Index].PressCount++;
            }
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Render current input state at the top of the console.
 *
 * @param PosX Mouse X position.
 * @param PosY Mouse Y position.
 * @param Buttons Button state bitmask (MB_*).
 */
static void UpdateInputDisplay(I32 PosX, I32 PosY, U32 Buttons, U32 LastKeyDown, U32 LastKeyUp) {
    POINT Cursor;
    U32 Index = 0;
    U32 Left = (Buttons & MB_LEFT) ? 1 : 0;
    U32 Right = (Buttons & MB_RIGHT) ? 1 : 0;
    U32 Middle = (Buttons & MB_MIDDLE) ? 1 : 0;

    Cursor.X = 0;
    Cursor.Y = 0;
    ConsoleGotoXY(&Cursor);

    printf("Mouse position: X=%d Y=%d                      \n", PosX, PosY);
    printf("Mouse buttons: L=%u R=%u M=%u                  \n", Left, Right, Middle);
    printf("Last keydown VK: 0x%02X  Last keyup VK: 0x%02X  \n", LastKeyDown & 0xFF, LastKeyUp & 0xFF);
    printf("Media keys:\n");
    for (Index = 0; Index < (sizeof(MediaIndicators) / sizeof(MediaIndicators[0])); Index++) {
        printf("  %-10s : %-4s count=%u      \n",
               MediaIndicators[Index].Name,
               MediaIndicators[Index].Pressed ? "DOWN" : "UP",
               MediaIndicators[Index].PressCount);
    }
}

/************************************************************************/

/**
 * @brief Entry point for the input info application.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Exit code.
 */
int main(int argc, char** argv) {
    MESSAGE Message;
    I32 PosX = 0;
    I32 PosY = 0;
    U32 Buttons = 0;
    U32 LastKeyDown = 0;
    U32 LastKeyUp = 0;

    UNUSED(argc);
    UNUSED(argv);

    ConsoleClear();
    UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);

    while (GetMessage(NULL, &Message, 0, 0)) {
        switch (Message.Message) {
            case EWM_MOUSEMOVE:
                PosX = (I32)Message.Param1;
                PosY = (I32)Message.Param2;
                UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);
                break;

            case EWM_MOUSEDOWN:
                Buttons |= Message.Param1;
                UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);
                break;

            case EWM_MOUSEUP:
                Buttons &= ~Message.Param1;
                UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);
                break;

            case EWM_KEYDOWN:
                LastKeyDown = Message.Param1;
                if (UpdateMediaIndicator(Message.Param1, TRUE)) {
                    UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);
                }
                break;

            case EWM_KEYUP:
                LastKeyUp = Message.Param1;
                if (UpdateMediaIndicator(Message.Param1, FALSE)) {
                    UpdateInputDisplay(PosX, PosY, Buttons, LastKeyDown, LastKeyUp);
                }
                break;

            default:
                break;
        }
    }

    return 0;
}

/************************************************************************/
