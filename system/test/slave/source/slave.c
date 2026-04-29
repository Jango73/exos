
/************************************************************************\

    EXOS Test program - Slave
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


    Slave - Test program with multiple tasks for process lifecycle testing

\************************************************************************/

#include "../../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../../runtime/include/exos/exos.h"

/***************************************************************************/

void WorkerTask(void* arg) {
    int TaskId = (int)(long)arg;
    unsigned int WorkTime;
    unsigned int IterationCount = 0;
    unsigned int i, calculation = 0;

    debug("WorkerTask %d: ENTER", TaskId);
    printf("Task %d starting\n", TaskId);

    srand(1234 + TaskId * 567);

    while (IterationCount < 2) {
        debug("WorkerTask %d: loop iteration %d starting", TaskId, IterationCount);

        WorkTime = 2000 + (rand() % 4000);

        printf("Task %d working for %d ms (iteration %d)\n", TaskId, WorkTime, IterationCount + 1);

        unsigned int ElapsedTime = 0;
        while (ElapsedTime < WorkTime) {
            for (i = 0; i < 10000; i++) {
                calculation += i * TaskId;
            }
            sleep(100);
            ElapsedTime += 100;
        }

        printf(
            "Task %d completed iteration %d (calculation result: %d)\n", TaskId, IterationCount + 1,
            calculation % 1000);
        IterationCount++;
    }

    printf("Task %d finished after %d iterations\n", TaskId, IterationCount);
    debug("WorkerTask %d: about to EXIT function", TaskId);
}

/***************************************************************************/

int main(int argc, char** argv) {
    int Thread1, Thread2, Thread3;

    UNUSED(argc);
    UNUSED(argv);

    debug("main: ENTER");

    printf("Slave process starting...\n");
    debug("main: printf starting done");

    printf("Creating 3 worker tasks...\n");
    debug("main: printf creating done");

    debug("main: calling _beginthread for task 1");
    Thread1 = _beginthread(WorkerTask, 65536, (void*)1);
    debug("main: _beginthread for task 1 returned %d", Thread1);

    debug("main: calling _beginthread for task 2");
    Thread2 = _beginthread(WorkerTask, 65536, (void*)2);
    debug("main: _beginthread for task 2 returned %d", Thread2);

    debug("main: calling _beginthread for task 3");
    Thread3 = _beginthread(WorkerTask, 65536, (void*)3);
    debug("main: _beginthread for task 3 returned %d", Thread3);

    if (Thread1 != 0 && Thread2 != 0 && Thread3 != 0) {
        printf("All tasks created successfully\n");
        debug("main: all tasks created successfully");
    } else {
        printf("Failed to create one or more tasks\n");
        debug("main: failed to create tasks: T1=%d, T2=%d, T3=%d", Thread1, Thread2, Thread3);
    }

    printf("Slave process exiting\n");
    debug("main: printf exiting done");
    debug("main: about to EXIT main function");
    return 0;
}
