
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
#include "tt-map.h"
#include "tt-fog.h"
#include "tt-entities.h"
#include "tt-commands.h"
#include "tt-ai.h"
#include "tt-render.h"
#include "tt-input.h"
#include "tt-save.h"
#include "tt-manual.h"
#include "tt-game.h"
#include "tt-production.h"

/************************************************************************/

static BOOL TryGetKey(I32* keyOut) {
    KEYCODE KeyCode;
    U32 gotKey;

    gotKey = ConsoleGetKey(&KeyCode);
    if (gotKey == 0) {
        return FALSE;
    }

    App.Input.LastKeyVK = (I32)KeyCode.VirtualKey;
    App.Input.LastKeyASCII = (I32)KeyCode.ASCIICode;
    App.Input.LastKeyModifiers = GetKeyModifiers();

    if (App.Input.LastKeyVK == 0 && App.Input.LastKeyASCII == 0) {
        return FALSE;
    }

    if (keyOut != NULL) {
        *keyOut = App.Input.LastKeyVK;
    }
    return TRUE;
}

/************************************************************************/

BOOL IsAreaVisible(I32 x, I32 y, I32 width, I32 height) {
    I32 mapW;
    I32 mapH;
    U8* visible;

    if (App.GameState == NULL || App.GameState->Terrain == NULL) return FALSE;
    if (App.GameState->SeeEverything) return TRUE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    visible = App.GameState->TeamData[HUMAN_TEAM_INDEX].VisibleNow;
    if (visible == NULL) return FALSE;

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
            if (visible[idx] == 0) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

static I32 CountTechCenters(I32 team) {
    BUILDING* building = IsValidTeam(team) ? App.GameState->TeamData[team].Buildings : NULL;
    I32 count = 0;

    while (building != NULL) {
        const BUILDING_TYPE* type = GetBuildingTypeById(building->TypeId);
        if (type != NULL &&
            type->Id == BUILDING_TYPE_TECH_CENTER &&
            building->Team == team &&
            !building->UnderConstruction &&
            IsBuildingPowered(building)) {
            count++;
        }
        building = building->Next;
    }
    return count;
}

/************************************************************************/

BOOL HasTechLevel(I32 requiredLevel, I32 team) {
    if (requiredLevel <= 1) return TRUE;
    return CountTechCenters(team) > 0;
}

/************************************************************************/

static BOOL IsAreaOnScreenAndVisible(I32 x, I32 y, I32 width, I32 height) {
    I32 screenX;
    I32 screenY;

    if (App.GameState == NULL) return FALSE;
    if (!GetScreenPosition(x, y, width, height, &screenX, &screenY)) return FALSE;
    if (!IsAreaVisible(x, y, width, height)) return FALSE;
    return TRUE;
}

/************************************************************************/

static void ClearSelection(void) {
    if (App.GameState == NULL) return;
    App.GameState->IsCommandMode = FALSE;
    App.GameState->CommandType = COMMAND_NONE;
    if (App.GameState->SelectedUnit != NULL) {
        App.GameState->SelectedUnit->IsSelected = FALSE;
        App.GameState->SelectedUnit = NULL;
    }
    App.GameState->SelectedBuilding = NULL;
    App.GameState->ProductionMenuActive = FALSE;
}

/************************************************************************/

/**
 * @brief Return the selected production building if any.
 */
static BUILDING* GetSelectedProductionBuilding(void) {
    if (App.GameState == NULL) return NULL;
    if (App.GameState->SelectedBuilding == NULL) return NULL;
    if (!IsProductionBuildingType(App.GameState->SelectedBuilding->TypeId)) return NULL;
    return App.GameState->SelectedBuilding;
}

/************************************************************************/

/**
 * @brief Render a status message for unit production failures.
 */
static void SetUnitProductionStatus(I32 result, const UNIT_TYPE* ut) {
    if (ut == NULL) return;
    switch (result) {
        case PRODUCTION_RESULT_QUEUE_FULL:
            SetStatus("Unit queue full (max 3)");
            return;
        case PRODUCTION_RESULT_TECH_LEVEL:
            if (ut->TechLevel >= 2) {
                SetStatus("Requires Tech Level 2 (build a Tech Center)");
            } else {
                SetStatus("Requires Tech Level 1");
            }
            return;
        case PRODUCTION_RESULT_RESOURCES: {
            char msg[MAX_SCREEN_WIDTH + 1];
            snprintf(msg, sizeof(msg), "Not enough plasma for %s (need %d)", ut->Name, ut->CostPlasma);
            SetStatus(msg);
            return;
        }
        default:
            SetStatus("Cannot queue unit");
            return;
    }
}

/************************************************************************/

/**
 * @brief Handle production menu input for a producer building.
 */
static BOOL HandleProductionMenuKey(BUILDING* producer, I32 key) {
    I32 optionCount = 0;
    const PRODUCTION_OPTION* options = GetProductionOptions(producer->TypeId, &optionCount);
    if (options == NULL || optionCount <= 0) return FALSE;

    for (I32 i = 0; i < optionCount; i++) {
        if (options[i].KeyVK != key) continue;
        if (options[i].IsBuilding) {
            if (EnqueuePlacement(options[i].TypeId)) return TRUE;
            return FALSE;
        }

        {
            I32 result = PRODUCTION_RESULT_OK;
            const UNIT_TYPE* ut = GetUnitTypeById(options[i].TypeId);
            if (EnqueueUnitProduction(producer, options[i].TypeId, producer->Team, &result)) {
                char msg[MAX_SCREEN_WIDTH + 1];
                snprintf(msg, sizeof(msg), "Queued %s", ut != NULL ? ut->Name : "unit");
                SetStatus(msg);
                return TRUE;
            }
            SetUnitProductionStatus(result, ut);
            return FALSE;
        }
    }

    return FALSE;
}

/************************************************************************/

static void CycleSelection(I32 direction) {
    const I32 maxEntries = MAX_MAP_SIZE + MAX_BUILDINGS;
    const BUILDING_TYPE* bt;
    const UNIT_TYPE* ut;
    const BUILDING* selectedBuilding = App.GameState != NULL ? App.GameState->SelectedBuilding : NULL;
    const UNIT* selectedUnit = App.GameState != NULL ? App.GameState->SelectedUnit : NULL;
    const void* entries[maxEntries];
    U8 entryIsBuilding[maxEntries];
    I32 count = 0;
    I32 currentIndex = -1;

    if (App.GameState == NULL) return;

    /* Collect human team units then buildings that are visible on screen */
    UNIT* unit = App.GameState->TeamData[HUMAN_TEAM_INDEX].Units;
    while (unit != NULL && count < maxEntries) {
        ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && IsAreaOnScreenAndVisible(unit->X, unit->Y, ut->Width, ut->Height)) {
            entries[count] = unit;
            entryIsBuilding[count] = 0;
            if (unit == selectedUnit) currentIndex = count;
            count++;
        }
        unit = unit->Next;
    }

    BUILDING* building = App.GameState->TeamData[HUMAN_TEAM_INDEX].Buildings;
    while (building != NULL && count < maxEntries) {
        bt = GetBuildingTypeById(building->TypeId);
        if (bt != NULL && IsAreaOnScreenAndVisible(building->X, building->Y, bt->Width, bt->Height)) {
            entries[count] = building;
            entryIsBuilding[count] = 1;
            if (building == selectedBuilding) currentIndex = count;
            count++;
        }
        building = building->Next;
    }

    if (count == 0) {
        ClearSelection();
        SetStatus("No visible team units or buildings on screen");
        return;
    }

    if (App.GameState->IsPlacingBuilding) {
        CancelBuildingPlacement();
    }

    if (direction == 0) direction = 1;
    I32 nextIndex = 0;
    if (currentIndex >= 0) {
        nextIndex = (currentIndex + (direction > 0 ? 1 : -1) + count) % count;
    }

    ClearSelection();
    if (entryIsBuilding[nextIndex]) {
        App.GameState->SelectedBuilding = (BUILDING*)entries[nextIndex];
    } else {
        App.GameState->SelectedUnit = (UNIT*)entries[nextIndex];
        App.GameState->SelectedUnit->IsSelected = TRUE;
    }
}

/************************************************************************/

static void GetViewportCenter(I32* centerX, I32* centerY) {
    I32 mapW;
    I32 mapH;

    if (App.GameState == NULL || centerX == NULL || centerY == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;

    *centerX = WrapCoord(App.GameState->ViewportPos.X, VIEWPORT_WIDTH / 2, mapW);
    *centerY = WrapCoord(App.GameState->ViewportPos.Y, VIEWPORT_HEIGHT / 2, mapH);
}

/************************************************************************/

static BOOL SelectNearestVisibleEntityByType(I32 typeId, BOOL isBuilding) {
    I32 mapW;
    I32 mapH;
    I32 centerX;
    I32 centerY;
    I32 bestDist = 0x7FFFFFFF;

    if (App.GameState == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    if (App.GameState->IsPlacingBuilding) {
        CancelBuildingPlacement();
    }

    GetViewportCenter(&centerX, &centerY);

    if (isBuilding) {
        BUILDING* best = NULL;
        BUILDING* building = App.GameState->TeamData[HUMAN_TEAM_INDEX].Buildings;
        while (building != NULL) {
            if (building->TypeId == typeId) {
                const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
                if (bt != NULL && IsAreaOnScreenAndVisible(building->X, building->Y, bt->Width, bt->Height)) {
                    I32 bx = WrapCoord(building->X, bt->Width / 2, mapW);
                    I32 by = WrapCoord(building->Y, bt->Height / 2, mapH);
                    I32 dist = ChebyshevDistance(centerX, centerY, bx, by, mapW, mapH);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = building;
                    }
                }
            }
            building = building->Next;
        }

        if (best != NULL) {
            ClearSelection();
            App.GameState->SelectedBuilding = best;
            return TRUE;
        }
        return FALSE;
    } else {
        UNIT* best = NULL;
        UNIT* unit = App.GameState->TeamData[HUMAN_TEAM_INDEX].Units;
        while (unit != NULL) {
            if (unit->TypeId == typeId) {
                const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
                if (ut != NULL && IsAreaOnScreenAndVisible(unit->X, unit->Y, ut->Width, ut->Height)) {
                    I32 ux = WrapCoord(unit->X, ut->Width / 2, mapW);
                    I32 uy = WrapCoord(unit->Y, ut->Height / 2, mapH);
                    I32 dist = ChebyshevDistance(centerX, centerY, ux, uy, mapW, mapH);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = unit;
                    }
                }
            }
            unit = unit->Next;
        }

        if (best != NULL) {
            ClearSelection();
            App.GameState->SelectedUnit = best;
            App.GameState->SelectedUnit->IsSelected = TRUE;
            return TRUE;
        }
        return FALSE;
    }
}

/************************************************************************/

void HandleMainMenuInput(I32 key) {
    switch (key) {
        case VK_N:
            App.Menu.CurrentMenu = MENU_NEW_GAME;
            App.Menu.SelectedOption = NEW_GAME_SELECT_WIDTH;
            break;

        case VK_ESCAPE:
            if (App.GameState != NULL) {
                App.Menu.CurrentMenu = MENU_IN_GAME;
            }
            break;

        case VK_L:
            LoadSaveList();
            if (App.Menu.SavedGameCount > 0) {
                App.Menu.SelectedSaveIndex = 0;
                App.Menu.CurrentMenu = MENU_LOAD;
            }
            break;

        case VK_S:
            if (App.GameState != NULL) {
                App.Menu.CurrentMenu = MENU_SAVE;
            }
            break;

        case VK_Q:
            App.Menu.ExitRequested = TRUE;
            break;

        case VK_M:
            App.Menu.MenuPage = 0;
            App.Menu.CurrentMenu = MENU_MANUAL;
            break;

    }
}

/************************************************************************/

void HandleNewGameInput(I32 key) {
    switch (key) {
        case VK_UP:
            App.Menu.SelectedOption--;
            if (App.Menu.SelectedOption < 0) App.Menu.SelectedOption = NEW_GAME_SELECT_COUNT - 1;
            break;

        case VK_DOWN:
            App.Menu.SelectedOption++;
            if (App.Menu.SelectedOption >= NEW_GAME_SELECT_COUNT) App.Menu.SelectedOption = 0;
            break;

        case VK_LEFT:
            switch (App.Menu.SelectedOption) {
                case NEW_GAME_SELECT_WIDTH:
                    App.Menu.PendingMapWidth -= MENU_MAP_SIZE_STEP;
                    if (App.Menu.PendingMapWidth < MIN_MAP_SIZE) App.Menu.PendingMapWidth = MAX_MAP_SIZE;
                    break;
                case NEW_GAME_SELECT_HEIGHT:
                    App.Menu.PendingMapHeight -= MENU_MAP_SIZE_STEP;
                    if (App.Menu.PendingMapHeight < MIN_MAP_SIZE) App.Menu.PendingMapHeight = MAX_MAP_SIZE;
                    break;
                case NEW_GAME_SELECT_TEAMS:
                    App.Menu.PendingTeamCount--;
                    if (App.Menu.PendingTeamCount < 2) App.Menu.PendingTeamCount = MAX_TEAMS;
                    break;
                case NEW_GAME_SELECT_DIFFICULTY:
                    App.Menu.PendingDifficulty = (App.Menu.PendingDifficulty + 2) % 3;
                    break;
            }
            break;

        case VK_RIGHT:
            switch (App.Menu.SelectedOption) {
                case NEW_GAME_SELECT_WIDTH:
                    App.Menu.PendingMapWidth += MENU_MAP_SIZE_STEP;
                    if (App.Menu.PendingMapWidth > MAX_MAP_SIZE) App.Menu.PendingMapWidth = MIN_MAP_SIZE;
                    break;
                case NEW_GAME_SELECT_HEIGHT:
                    App.Menu.PendingMapHeight += MENU_MAP_SIZE_STEP;
                    if (App.Menu.PendingMapHeight > MAX_MAP_SIZE) App.Menu.PendingMapHeight = MIN_MAP_SIZE;
                    break;
                case NEW_GAME_SELECT_TEAMS:
                    App.Menu.PendingTeamCount++;
                    if (App.Menu.PendingTeamCount > MAX_TEAMS) App.Menu.PendingTeamCount = 2;
                    break;
                case NEW_GAME_SELECT_DIFFICULTY:
                    App.Menu.PendingDifficulty = (App.Menu.PendingDifficulty + 1) % 3;
                    break;
            }
            break;

        case VK_ENTER:
            /* Start new game */
            CleanupGame();
            if (InitializeGame(App.Menu.PendingMapWidth, App.Menu.PendingMapHeight, App.Menu.PendingDifficulty, App.Menu.PendingTeamCount)) {
                App.Menu.CurrentMenu = MENU_IN_GAME;
            }
            break;

        case VK_ESCAPE:
            App.Menu.CurrentMenu = MENU_MAIN;
            break;
    }
}

/************************************************************************/

void HandleInGameInput(I32 key) {
    if (App.GameState != NULL && App.GameState->IsCommandMode) {
        switch (key) {
            case VK_ESCAPE:
                CancelUnitCommand();
                return;
            case VK_M:
                if (App.GameState->CommandType != COMMAND_ESCORT) {
                    ConfirmUnitCommand();
                    return;
                }
                break;
            case VK_E:
                if (App.GameState->CommandType == COMMAND_ESCORT) {
                    ConfirmUnitCommand();
                    return;
                }
                break;
            case VK_UP:
                MoveCommandCursor(0, -1);
                return;
            case VK_DOWN:
                MoveCommandCursor(0, 1);
                return;
            case VK_LEFT:
                MoveCommandCursor(-1, 0);
                return;
            case VK_RIGHT:
                MoveCommandCursor(1, 0);
                return;
        }
    }

    if (App.GameState != NULL && App.GameState->IsPlacingBuilding) {
        switch (key) {
            case VK_ESCAPE:
                CancelBuildingPlacement();
                return;
            case VK_P:
                ConfirmBuildingPlacement();
                return;
            case VK_UP:
                MovePlacement(0, -1);
                return;
            case VK_DOWN:
                MovePlacement(0, 1);
                return;
            case VK_LEFT:
                MovePlacement(-1, 0);
                return;
            case VK_RIGHT:
                MovePlacement(1, 0);
                return;
        }
    }

    if (key == VK_DELETE) {
        CancelSelectedBuildingProduction();
        return;
    }

    if (App.GameState != NULL && (App.Input.LastKeyModifiers & KEYMOD_ALT) != 0) {
        I32 targetType = 0;
        BOOL isBuilding = FALSE;

        switch (key) {
            case VK_T:
                targetType = UNIT_TYPE_TROOPER;
                isBuilding = FALSE;
                break;
            case VK_S:
                targetType = UNIT_TYPE_SOLDIER;
                isBuilding = FALSE;
                break;
            case VK_Y:
                targetType = BUILDING_TYPE_CONSTRUCTION_YARD;
                isBuilding = TRUE;
                break;
            case VK_F:
                targetType = BUILDING_TYPE_FACTORY;
                isBuilding = TRUE;
                break;
            case VK_A:
                targetType = UNIT_TYPE_TANK;
                isBuilding = FALSE;
                break;
            case VK_B:
                targetType = BUILDING_TYPE_BARRACKS;
                isBuilding = TRUE;
                break;
            case VK_R:
                targetType = UNIT_TYPE_TRANSPORT;
                isBuilding = FALSE;
                break;
            case VK_D:
                targetType = UNIT_TYPE_DRILLER;
                isBuilding = FALSE;
                break;
        }

        if (targetType != 0) {
            if (!SelectNearestVisibleEntityByType(targetType, isBuilding)) {
                const char* name = NULL;
                if (isBuilding) {
                    const BUILDING_TYPE* bt = GetBuildingTypeById(targetType);
                    name = (bt != NULL) ? bt->Name : "building";
                } else {
                    const UNIT_TYPE* ut = GetUnitTypeById(targetType);
                    name = (ut != NULL) ? ut->Name : "unit";
                }
                if (name != NULL) {
                    char msg[MAX_SCREEN_WIDTH + 1];
                    snprintf(msg, sizeof(msg), "No visible %s on screen", name);
                    SetStatus(msg);
                }
            }
            return;
        }
    }

    if (key == VK_PAGEDOWN) {
        CycleSelection(1);
        return;
    }
    if (key == VK_PAGEUP) {
        CycleSelection(-1);
        return;
    }

    if (key == VK_C) {
        if (App.GameState != NULL) {
            if (App.GameState->SelectedUnit != NULL) {
                const UNIT_TYPE* ut = GetUnitTypeById(App.GameState->SelectedUnit->TypeId);
                if (ut != NULL) {
                    CenterViewportOn(App.GameState->SelectedUnit->X + ut->Width / 2, App.GameState->SelectedUnit->Y + ut->Height / 2);
                }
            } else if (App.GameState->SelectedBuilding != NULL) {
                const BUILDING_TYPE* bt = GetBuildingTypeById(App.GameState->SelectedBuilding->TypeId);
                if (bt != NULL) {
                    CenterViewportOn(App.GameState->SelectedBuilding->X + bt->Width / 2, App.GameState->SelectedBuilding->Y + bt->Height / 2);
                }
            }
        }
        return;
    }

    if (key == VK_UP) {
        MoveViewport(0, -1);
        return;
    }
    if (key == VK_DOWN) {
        MoveViewport(0, 1);
        return;
    }
    if (key == VK_LEFT) {
        MoveViewport(-1, 0);
        return;
    }
    if (key == VK_RIGHT) {
        MoveViewport(1, 0);
        return;
    }

    if (key == VK_F1) {
        App.Menu.CurrentMenu = MENU_DEBUG;
        App.Menu.PrevMenu = -1;
        ResetRenderCache();
        return;
    }

    if (key == VK_F4) {
        if (App.GameState != NULL) {
            App.GameState->GhostMode = !App.GameState->GhostMode;
            SetStatus(App.GameState->GhostMode ? "Ghost mode enabled" : "Ghost mode disabled");
            LogTeamAction(HUMAN_TEAM_INDEX, "GhostMode", 0,
                          (U32)(App.GameState->GhostMode ? 1 : 0), 0, "", "");
        }
        return;
    }

    if (key == VK_F5) {
        if (App.GameState != NULL) {
            SpawnDebugBaseForAllTeams();
        }
        return;
    }

    if (key == VK_PLUS || key == VK_MINUS) {
        if (App.GameState != NULL) {
            char msg[MAX_SCREEN_WIDTH + 1];
            if (key == VK_PLUS) {
                App.GameState->GameSpeed++;
            } else if (App.GameState->GameSpeed > 1) {
                App.GameState->GameSpeed--;
            }
            snprintf(msg, sizeof(msg), "Game speed: %d", App.GameState->GameSpeed);
            SetStatus(msg);
        }
        return;
    }

    if (App.GameState != NULL && App.GameState->SelectedUnit != NULL) {
        if (key == VK_M) {
            StartUnitCommand(COMMAND_MOVE);
            return;
        }
        if (key == VK_A) {
            StartUnitCommand(COMMAND_ATTACK);
            return;
        }
        if (key == VK_E) {
            StartUnitCommand(COMMAND_ESCORT);
            return;
        }
        if (key == VK_X) {
            UNIT* unit = App.GameState->SelectedUnit;
            const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
            I32 tx;
            I32 ty;
            BOOL found = FALSE;
            if (ut != NULL && ut->Id == UNIT_TYPE_DRILLER) {
                found = FindNearestPlasmaCell(unit->X, unit->Y, &tx, &ty);
            } else {
                found = PickExplorationTarget(unit->Team, &tx, &ty);
            }
            if (found) {
                SetUnitStateExplore(unit, tx, ty);
                SetStatus("Exploration engaged");
            } else {
                SetStatus("No exploration target found");
            }
            return;
        }
    }

    if (key == VK_C) {
        App.GameState->ShowCoordinates = !App.GameState->ShowCoordinates;
        return;
    }

    if (ENABLE_CHEATS) {
        if (key == VK_F2) {
            if (App.GameState != NULL && App.GameState->Terrain != NULL) {
                for (I32 y = 0; y < App.GameState->MapHeight; y++) {
                    for (I32 x = 0; x < App.GameState->MapWidth; x++) {
                        TerrainSetVisible(&App.GameState->Terrain[y][x], TRUE);
                    }
                }
                App.GameState->SeeEverything = TRUE;
                App.GameState->FogDirty = TRUE;
                SetStatus("Map revealed, omniscient view enabled");
            }
            return;
        }

        if (key == VK_F3) {
            TEAM_RESOURCES* res = GetTeamResources(HUMAN_TEAM_INDEX);
            if (res != NULL) {
                {
                    char msg[MAX_SCREEN_WIDTH + 1];
                    res->Plasma += CHEAT_PLASMA_AMOUNT;
                    snprintf(msg, sizeof(msg), "Plasma boosted by %d", CHEAT_PLASMA_AMOUNT);
                    SetStatus(msg);
                }
            }
            return;
        }
    }

    if (key == VK_S) {
        App.Menu.CurrentMenu = MENU_SAVE;
        return;
    }

    if (key == VK_L) {
        LoadSaveList();
        if (App.Menu.SavedGameCount > 0) {
            App.Menu.SelectedSaveIndex = 0;
            App.Menu.CurrentMenu = MENU_LOAD;
        }
        return;
    }

    if (key == VK_SPACE) {
        App.GameState->IsPaused = !App.GameState->IsPaused;
        return;
    }

    {
        BUILDING* producer = GetSelectedProductionBuilding();
        if (producer != NULL) {
            if (key == VK_B) {
                App.GameState->ProductionMenuActive = TRUE;
                return;
            }
            if (producer->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD && key == VK_P) {
                StartPlacementFromQueue();
                return;
            }
            if (App.GameState->ProductionMenuActive) {
                if (key == VK_ESCAPE) {
                    App.GameState->ProductionMenuActive = FALSE;
                    SetStatus(" ");
                    return;
                }
                if (HandleProductionMenuKey(producer, key)) {
                    App.GameState->ProductionMenuActive = FALSE;
                }
                return;
            }
        }
    }

    if (key == VK_ESCAPE) {
        App.Menu.CurrentMenu = MENU_MAIN;
        return;
    }
}

/************************************************************************/

void HandleSaveInput(I32 key) {
    I32 len = (I32)strlen(App.Menu.SaveFileName);
    char ascii = (char)(App.Input.LastKeyASCII & 0xFF);

    if (key == VK_ESCAPE) {
        App.Menu.CurrentMenu = MENU_MAIN;
        return;
    }

    if (key == VK_ENTER) {
        if (len > 0 && App.GameState != NULL) {
            if (SaveGame(App.Menu.SaveFileName)) {
                LoadSaveList();
            }
        }
        App.Menu.CurrentMenu = MENU_MAIN;
        return;
    }

    if (key == VK_BACKSPACE) {
        if (len > 0) {
            App.Menu.SaveFileName[len - 1] = '\0';
        }
        return;
    }

    if (len < (NAME_MAX_LENGTH - 1) && IsValidFilenameChar(ascii)) {
        App.Menu.SaveFileName[len] = ascii;
        App.Menu.SaveFileName[len + 1] = '\0';
    }
}

/************************************************************************/

void HandleLoadInput(I32 key) {
    if (key == VK_ESCAPE) {
        App.Menu.CurrentMenu = MENU_MAIN;
        return;
    }

    if (key == VK_UP && App.Menu.SavedGameCount > 0) {
        if (App.Menu.SelectedSaveIndex > 0) App.Menu.SelectedSaveIndex--;
        return;
    }
    if (key == VK_DOWN && App.Menu.SavedGameCount > 0) {
        if (App.Menu.SelectedSaveIndex < App.Menu.SavedGameCount - 1) App.Menu.SelectedSaveIndex++;
        return;
    }

    if (key == VK_ENTER && App.Menu.SavedGameCount > 0) {
        if (LoadGame(App.Menu.SavedGames[App.Menu.SelectedSaveIndex])) {
            App.Menu.CurrentMenu = MENU_IN_GAME;
            App.Menu.PrevMenu = -1;
        } else {
            App.Menu.CurrentMenu = MENU_MAIN;
        }
        return;
    }
}

/************************************************************************/

/// @brief Handle input for the manual screen.
void HandleManualInput(I32 key) {
    I32 visibleLines = MANUAL_CONTENT_BOTTOM - MANUAL_CONTENT_TOP + 1;
    I32 maxScroll = GetManualScrollMax(visibleLines);
    I32 pageSize = visibleLines;

    switch (key) {
        case VK_ESCAPE:
            App.Menu.CurrentMenu = MENU_MAIN;
            return;
        case VK_UP:
            App.Menu.MenuPage--;
            break;
        case VK_DOWN:
            App.Menu.MenuPage++;
            break;
        case VK_PAGEUP:
            App.Menu.MenuPage -= pageSize;
            break;
        case VK_PAGEDOWN:
            App.Menu.MenuPage += pageSize;
            break;
        case VK_HOME:
            App.Menu.MenuPage = 0;
            break;
        case VK_END:
            App.Menu.MenuPage = maxScroll;
            break;
        default:
            return;
    }

    if (App.Menu.MenuPage < 0) App.Menu.MenuPage = 0;
    if (App.Menu.MenuPage > maxScroll) App.Menu.MenuPage = maxScroll;
}

/************************************************************************/

void ProcessInput(void) {
    I32 key;

    while (TryGetKey(&key)) {
        switch (App.Menu.CurrentMenu) {
            case MENU_MAIN:
                if (App.GameState != NULL && key == VK_ESCAPE) {
                    App.Menu.CurrentMenu = MENU_IN_GAME;
                } else {
                    HandleMainMenuInput(key);
                }
                break;

            case MENU_NEW_GAME:
                HandleNewGameInput(key);
                break;

            case MENU_IN_GAME:
                HandleInGameInput(key);
                break;

            case MENU_SAVE:
                HandleSaveInput(key);
                break;

            case MENU_LOAD:
                HandleLoadInput(key);
                break;

            case MENU_MANUAL:
                HandleManualInput(key);
                break;

            case MENU_DEBUG:
                if (key == VK_ESCAPE) {
                    App.Menu.CurrentMenu = MENU_IN_GAME;
                    App.Menu.PrevMenu = -1;
                    ResetRenderCache();
                }
                break;
            case MENU_GAME_OVER:
                if (key == VK_ESCAPE) {
                    CleanupGame();
                    App.Menu.CurrentMenu = MENU_MAIN;
                    App.Menu.PrevMenu = -1;
                }
                break;

            default:
                /* Handle common keys */
                if (key == VK_ESCAPE) {
                    App.Menu.CurrentMenu = MENU_MAIN;
                }
                break;
        }
    }
}
