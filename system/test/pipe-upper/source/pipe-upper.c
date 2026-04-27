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

#include "../../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../../runtime/include/exos/exos.h"
#include "../../../../runtime/include/stdlib/unistd.h"

/***************************************************************************/

static char PipeUpperToUpper(char Character) {
    if (Character >= 'a' && Character <= 'z') {
        return (char)(Character - ('a' - 'A'));
    }

    return Character;
}

/***************************************************************************/

int main(int argc, char** argv) {
    char Buffer[128];
    ssize_t ReadSize;
    ssize_t Index;

    UNUSED(argc);
    UNUSED(argv);

    while (TRUE) {
        ReadSize = read(STDIN_FILENO, Buffer, sizeof(Buffer));
        if (ReadSize <= 0) {
            break;
        }

        for (Index = 0; Index < ReadSize; Index++) {
            Buffer[Index] = PipeUpperToUpper(Buffer[Index]);
        }

        write(STDOUT_FILENO, Buffer, (size_t)ReadSize);
    }

    return 0;
}

/***************************************************************************/
