
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


    Hello - Simple hello world program

\************************************************************************/

#include "../../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../../runtime/include/exos/exos.h"

/***************************************************************************/

int main(int argc, char** argv) {
    int PauseEnabled = TRUE;
    int Index = 0;

    for (Index = 1; Index < argc; Index++) {
        if (strcmp(argv[Index], "--no-pause") == 0) {
            PauseEnabled = FALSE;
        }
    }

    printf("Shall I compare thee to a summer's day?\n");
    printf("Thou art more lovely and more temperate:\n");
    printf("Rough winds do shake the darling buds of May,\n");
    printf("And summer's lease hath all too short a date:\n\n");

    if (PauseEnabled) Sleep(2000);

    printf("Sometime too hot the eye of heaven shines,\n");
    printf("And often is his gold complexion dimm'd;\n");
    printf("And every fair from fair sometime declines,\n");
    printf("By chance, or nature's changing course, untrimm'd;\n\n");

    if (PauseEnabled) Sleep(2000);

    printf("But thy eternal summer shall not fade,\n");
    printf("Nor lose possession of that fair thou ow'st,\n");
    printf("Nor shall death brag thou wander'st in his shade,\n");
    printf("When in eternal lines to time thou grow'st:\n\n");

    if (PauseEnabled) Sleep(2000);

    printf("So long as men can breathe or eyes can see,\n");
    printf("So long lives this, and this gives life to thee.\n\n");
    printf("-- William Shakespeare, Sonnet 18\n\n");

    return 0xDEAD;
}

/***************************************************************************/
