/************************************************************************\

    EXOS test program
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

\************************************************************************/

#include "../../../../runtime/include/exos-runtime.h"
#include "../../../../runtime/include/exos.h"
#include "../../../../runtime/include/stdio.h"
#include "../../../../runtime/include/unistd.h"

/***************************************************************************/

int main(int argc, char** argv) {
    char Buffer[128];
    char Initials[64];
    ssize_t ReadSize;
    ssize_t Index;
    UINT InitialCount = 0;
    BOOL LineStart = TRUE;

    UNUSED(argc);
    UNUSED(argv);

    while (TRUE) {
        ReadSize = read(STDIN_FILENO, Buffer, sizeof(Buffer));
        if (ReadSize <= 0) {
            break;
        }

        for (Index = 0; Index < ReadSize; Index++) {
            char Character = Buffer[Index];

            if (LineStart && Character != '\r' && Character != '\n') {
                if (InitialCount < sizeof(Initials) - 1) {
                    Initials[InitialCount++] = Character;
                }
                LineStart = FALSE;
            }

            if (Character == '\n') {
                LineStart = TRUE;
            }
        }
    }

    Initials[InitialCount] = '\0';
    debug("PIPE_SMOKE_RESULT:%s", Initials);
    printf("PIPE_SMOKE_RESULT:%s\n", Initials);
    return 0;
}

/***************************************************************************/
