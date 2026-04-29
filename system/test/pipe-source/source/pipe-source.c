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
#include "../../../../runtime/include/stdlib/string.h"
#include "../../../../runtime/include/stdlib/unistd.h"

/***************************************************************************/

int main(int argc, char** argv) {
    const char* Payload = "red\nblue\ngreen\n";
    size_t Length = strlen(Payload);
    UNUSED(argc);
    UNUSED(argv);
    write(STDOUT_FILENO, Payload, Length);
    return 0;
}

/***************************************************************************/
