
/************************************************************************\

    EXOS Sample program - Terminal Tactics
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


    Terminal Tactics - Terminal Strategy Game

\************************************************************************/

#include "tt-types.h"
#include "tt-input.h"
#include "tt-game.h"
#include "tt-render.h"
#include "tt-save.h"

/************************************************************************/

INT main(INT argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    App.Menu.CurrentMenu = MENU_MAIN;
    App.Menu.SelectedOption = 0;
    App.Menu.MenuPage = 0;
    App.Menu.ExitRequested = FALSE;
    LoadSaveList();

    EnsureScreenMetrics();

    while ((App.GameState == NULL || App.GameState->IsRunning) && !App.Menu.ExitRequested) {
        EnsureScreenMetrics();
        ProcessInput();
        UpdateGame();
        RenderScreen();
        Sleep(5);
    }

    CleanupGame();
    ConsoleClear();
    return 0;
}
