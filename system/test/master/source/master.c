
/************************************************************************\

    EXOS Test program - Master
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


    Master - Test program for process lifecycle management

\************************************************************************/

#include "../../../../runtime/include/exos-runtime.h"
#include "../../../../runtime/include/exos.h"

/***************************************************************************/

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    debug("[Master main] Master process starting...");
    printf("Master process starting...\n");

    // Test file writing functionality
    debug("[Master main] Testing file write...");
    printf("Testing file write...\n");

    FILE* testFile = fopen("test.txt", "w");
    if (testFile) {
        debug("[Master main] File opened successfully");
        printf("File opened successfully\n");

        int bytesWritten = fprintf(testFile, "Hello from EXOS!\nThis is a test file.\nLine 3\n");
        debug("[Master main] fprintf returned: %d", bytesWritten);
        printf("Wrote %d bytes\n", bytesWritten);

        debug("[Master main] Closing file");
        fclose(testFile);
        printf("File closed\n");
    } else {
        debug("[Master main] Failed to open file for writing");
        printf("Failed to open file for writing\n");
    }

    debug("[Master main] Launching slave process...");
    printf("Launching slave process...\n");

    int result = system("/package/binary/slave");

    if (result == DF_RETURN_SUCCESS) {
        printf("Slave process launched successfully\n");
    } else {
        printf("Failed to launch slave process (result: %d)\n", result);
    }

    sleep(500);

    printf("Master process exiting\n");

    return 0;
}
