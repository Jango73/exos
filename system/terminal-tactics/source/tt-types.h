
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

#ifndef TT_TYPES_H
#define TT_TYPES_H

#include "../../../kernel/include/User.h"
#include "../../../kernel/include/input/VKey.h"
#include "../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../runtime/include/exos/exos.h"

#define MAX_SCREEN_WIDTH 160
#define MAX_SCREEN_HEIGHT 60
#define TOP_BAR_HEIGHT 1
#define BOTTOM_BAR_HEIGHT 5
#define MAX_MAP_VIEW_HEIGHT (MAX_SCREEN_HEIGHT - TOP_BAR_HEIGHT - BOTTOM_BAR_HEIGHT)
#define MAX_VIEWPORT_WIDTH (MAX_SCREEN_WIDTH)
#define MAX_VIEWPORT_HEIGHT (MAX_MAP_VIEW_HEIGHT)
#define SCREEN_WIDTH (App.Render.ScreenWidth)
#define SCREEN_HEIGHT (App.Render.ScreenHeight)
#define MAP_VIEW_HEIGHT (App.Render.MapViewHeight)
#define VIEWPORT_WIDTH (App.Render.ViewportWidth)
#define VIEWPORT_HEIGHT (App.Render.ViewportHeight)
#define NEW_GAME_SELECT_WIDTH 0
#define NEW_GAME_SELECT_HEIGHT 1
#define NEW_GAME_SELECT_TEAMS 2
#define NEW_GAME_SELECT_DIFFICULTY 3
#define NEW_GAME_SELECT_COUNT 4
#define MAX_PLACEMENT_QUEUE 3
#define MAX_UNIT_QUEUE 3

#define MIN_MAP_SIZE 50
#define MAX_MAP_SIZE 200
#define DEFAULT_MAP_SIZE 100
#define MENU_MAP_SIZE_STEP 10
#define MAX_TEAMS 5
#define HUMAN_TEAM_INDEX 0
#define DEFAULT_AI_TEAMS 1
#define AI_THREAT_RADIUS_DEFAULT 6
#define TEAM_START_ZONE_DIVISOR 4
#define TEAM_START_ZONE_HALF_DIVISOR 2
#define TEAM_START_ZONE_THREE_QUARTERS_NUM 3
#define TEAM_START_SEARCH_RADIUS 6
#define TEAM_START_ESCAPE_RADIUS 15

#define AI_ATTITUDE_AGGRESSIVE 1
#define AI_ATTITUDE_DEFENSIVE 2

#define AI_MINDSET_IDLE 0
#define AI_MINDSET_URGENCY 1
#define AI_MINDSET_PANIC 2
#define AI_UPDATE_INTERVAL_MS 500
#define AI_UPDATE_INTERVAL_EASY_MS 8000
#define AI_UPDATE_INTERVAL_NORMAL_MS 4000
#define AI_UPDATE_INTERVAL_HARD_MS 0
#define AI_DRILLER_ALERT_MS 3000
#define AI_DRILLER_ESCORT_FORCE_DIVISOR 2
#define AI_DAMAGE_REDUCTION_MIN 10
#define AI_DAMAGE_REDUCTION_MAX 30
#define AI_DAMAGE_REDUCTION_DIVISOR 20
#define AI_UNIT_SCORE_DAMAGE_WEIGHT 1000
#define AI_FORTRESS_AGGRESSIVE_CHANCE_PERCENT 35
#define AI_PERCENT_BASE 100
#define AI_ENERGY_LOW_MAX 50
#define AI_MOBILE_TARGET_PANIC 4
#define AI_MOBILE_TARGET_URGENCY 6
#define AI_MOBILE_TARGET_IDLE 8
#define AI_IDLE_MIN_DEFENSE 4
#define AI_DRILLER_TARGET_COUNT 2
#define AI_DRILLER_PER_NON_DRILLER 30
#define AI_SCOUT_TARGET_COUNT 2
#define AI_BASE_SHUFFLE_RADIUS 8
#define AI_BASE_SHUFFLE_COUNT 3
#define AI_BASE_SHUFFLE_COOLDOWN_MS 10000
#define AI_ATTITUDE_RANDOM_THRESHOLD 0.5f
#define DRILLER_HARVEST_AMOUNT 40
#define DRILLER_HARVEST_INTERVAL_MS 10000
#define AI_CLUSTER_UPDATE_INTERVAL_MS 5000

#define FOG_OF_WAR_UPDATE_INTERVAL_MS 2000
#define FOG_OF_WAR_SIGHT_RADIUS 5
#define UNIT_MOVE_TIME_MS 2000
#define UNIT_STUCK_BACKOFF_TILES 5
#define UNIT_STUCK_TIMEOUT_MULTIPLIER 3
#define UNIT_GRIDLOCK_MOVE_LIMIT 3
#define AI_LAST_DECISION_LEN 64
#define ENABLE_CHEATS 1
#define COMMAND_NONE 0
#define COMMAND_MOVE 1
#define COMMAND_ATTACK 2
#define COMMAND_ESCORT 3

#define UNIT_STATE_IDLE 0
#define UNIT_STATE_ESCORT 1
#define UNIT_STATE_EXPLORE 2

#define UNIT_STATE_TARGET_NONE -1
#define UNIT_STATE_UPDATE_INTERVAL_MS 500

#define TERRAIN_TYPE_PLAINS 0
#define TERRAIN_TYPE_MOUNTAIN 1
#define TERRAIN_TYPE_FOREST 2
#define TERRAIN_TYPE_WATER 3
#define TERRAIN_TYPE_PLASMA 4

#define TERRAIN_CHAR_PLAINS ' '
#define TERRAIN_CHAR_MOUNTAIN '^'
#define TERRAIN_CHAR_FOREST '*'
#define TERRAIN_CHAR_WATER '~'
#define TERRAIN_CHAR_PLASMA '$'

#define TERRAIN_TYPE_MASK 0x3F
#define TERRAIN_FLAG_OCCUPIED 0x40
#define TERRAIN_FLAG_VISIBLE 0x80

#define BUILDING_TYPE_CONSTRUCTION_YARD 1
#define BUILDING_TYPE_BARRACKS 2
#define BUILDING_TYPE_POWER_PLANT 3
#define BUILDING_TYPE_FACTORY 4
#define BUILDING_TYPE_TECH_CENTER 5
#define BUILDING_TYPE_TURRET 6
#define BUILDING_TYPE_WALL 7
#define BUILDING_TYPE_COUNT 7

#define UNIT_TYPE_TROOPER 1
#define UNIT_TYPE_SOLDIER 2
#define UNIT_TYPE_ENGINEER 3
#define UNIT_TYPE_SCOUT 4
#define UNIT_TYPE_MOBILE_ARTILLERY 5
#define UNIT_TYPE_TANK 6
#define UNIT_TYPE_TRANSPORT 7
#define UNIT_TYPE_DRILLER 8
#define UNIT_TYPE_COUNT 8

#define DIFFICULTY_EASY 0
#define DIFFICULTY_NORMAL 1
#define DIFFICULTY_HARD 2
#define START_PLASMA_EASY 2200
#define START_PLASMA_NORMAL 1100
#define START_PLASMA_HARD 550
#define START_ENERGY_EASY 200
#define START_ENERGY_NORMAL 100
#define START_ENERGY_HARD 50
#define START_MAX_ENERGY_EASY 500
#define START_MAX_ENERGY_NORMAL 300
#define START_MAX_ENERGY_HARD 200
#define MEMORY_CELL_NONE 0

#define MAX_BUILDINGS 100
#define NAME_MAX_LENGTH 64
#define MAX_SAVED_GAMES 128
#define UNIT_ATTACK_INTERVAL_MS 1000
#define ENABLE_PATHFINDING 0
#define EXPLORE_FIND_ATTEMPTS 64
#define CHEAT_PLASMA_AMOUNT 1000
#define UI_COMBAT_FLASH_MS 1000
#define UI_HP_MAX_DISPLAY 999
#define UI_HP_2_DIGITS_MIN 10
#define UI_HP_3_DIGITS_MIN 100
#define UI_HP_BUFFER_SIZE 4
#define UI_TWO_DIGIT_MIN 10
#define UI_DECIMAL_BASE 10
#define UI_DECIMAL_BASE_SQUARED 100
#define UI_MS_PER_SECOND 1000
#define UI_BUILD_TIME_ROUND_MS 999
#define UI_BUILD_TIME_MAX_SECONDS 99
#define UNIT_DEPLOY_WARN_INTERVAL_MS 2000
#define UNIT_DEPLOY_RADIUS 6
#define START_DRILLER_SPAWN_RADIUS 6
#define BUILDING_AUTOPLACE_RADIUS 6
#define BUILDING_AUTOPLACE_MARGIN 2
#define ESCORT_SPAWN_RADIUS 3
#define GAME_TIME_MS_PER_DAY 60000
#define UI_KEYINFO_SIZE 32
#define UI_TOKEN_SIZE 64
#define UI_SUFFIX_SIZE 32
#define UI_SAVE_LABEL_SIZE 32
#define MAIN_MENU_TITLE_Y 5
#define MAIN_MENU_OPTION_START_Y 7
#define MAIN_MENU_OPTION_STEP_Y 2
#define NEW_GAME_TITLE_Y 4
#define NEW_GAME_WIDTH_Y 8
#define NEW_GAME_HEIGHT_Y 10
#define NEW_GAME_TEAMS_Y 12
#define NEW_GAME_DIFFICULTY_Y 14
#define NEW_GAME_FOOTER_Y 18
#define LOAD_GAME_TITLE_Y 4
#define LOAD_GAME_START_Y 6
#define LOAD_GAME_EMPTY_OFFSET 3
#define LOAD_GAME_MAX_ITEMS 10
#define GAME_OVER_TITLE_Y 4
#define GAME_OVER_MESSAGE_Y 6
#define GAME_OVER_LIST_START_Y 8
#define GAME_OVER_LIST_STEP_Y 1
#define GAME_OVER_FOOTER_Y 20
#define MANUAL_TITLE_Y 1
#define MANUAL_CONTENT_TOP 3
#define MANUAL_CONTENT_BOTTOM (SCREEN_HEIGHT - 3)
#define MANUAL_FOOTER_Y (SCREEN_HEIGHT - 2)

#define SCORE_UNIT_HP_WEIGHT 1
#define SCORE_UNIT_DAMAGE_WEIGHT 10
#define SCORE_BUILDING_HP_WEIGHT 1
#define SCORE_BUILDING_COST_WEIGHT 1
#define MAP_NOISE_SCALE 10.0f
#define STATUS_MESSAGE_DURATION_MS 5000

typedef struct {
    I32 X;
    I32 Y;
} POINT_2D;

#pragma pack(push, 1)
typedef struct {
    U8 TerrainType : 3;
    U8 TerrainKnown : 1;
    U8 TerrainReserved : 4;
    U8 OccupiedType : 4;
    U8 IsBuilding : 1;
    U8 Team : 3;
} MEMORY_CELL;
#pragma pack(pop)

typedef struct BUILDING_STRUCT BUILDING;
typedef struct UNIT_STRUCT UNIT;
typedef struct PATH_NODE_STRUCT PATH_NODE;

typedef struct {
    U8 Bits;
} TERRAIN;

typedef struct {
    I32 Id;
    char Symbol;
    const char* Name;
    const char* Icon;
    I32 Width;
    I32 Height;
    I32 MaxHp;
    I32 Armor;
    I32 Damage;
    I32 Range;
    I32 AttackSpeed;
    I32 CostPlasma;
    I32 EnergyConsumption;
    I32 EnergyProduction;
    I32 TechLevel;
    I32 BuildTime;
} BUILDING_TYPE;

typedef struct {
    I32 Id;
    char Symbol;
    const char* Name;
    const char* Icon;
    I32 Width;
    I32 Height;
    I32 MaxHp;
    I32 Speed;
    I32 Damage;
    I32 Range;
    I32 Sight;
    I32 MoveTimeMs;
    I32 CostPlasma;
    I32 Armor;
    I32 TechLevel;
    I32 BuildTime;
    I32 AttackSpeed;
} UNIT_TYPE;

typedef struct {
    I32 Plasma;
    I32 Energy;
    I32 MaxEnergy;
} TEAM_RESOURCES;

typedef struct {
    I32 TypeId;
    U32 TimeRemaining;
} BUILD_JOB;

typedef struct {
    I32 TypeId;
    U32 TimeRemaining;
} UNIT_JOB;

struct BUILDING_STRUCT {
    I32 Id;
    I32 TypeId;
    I32 X, Y;
    I32 Hp;
    I32 Team;
    I32 Level;
    U32 BuildTimeRemaining;
    BOOL UnderConstruction;
    BUILD_JOB BuildQueue[MAX_PLACEMENT_QUEUE];
    I32 BuildQueueCount;
    UNIT_JOB UnitQueue[MAX_UNIT_QUEUE];
    I32 UnitQueueCount;
    U32 LastDamageTime;
    U32 LastAttackTime;
    struct BUILDING_STRUCT* Next;
};

struct UNIT_STRUCT {
    I32 Id;
    I32 TypeId;
    I32 X, Y;
    I32 Hp;
    I32 Team;
    I32 State;
    I32 EscortUnitId;
    I32 EscortUnitTeam;
    I32 StateTargetX;
    I32 StateTargetY;
    BOOL IsMoving;
    I32 TargetX;
    I32 TargetY;
    BOOL IsSelected;
    U32 LastAttackTime;
    U32 LastDamageTime;
    U32 LastHarvestTime;
    U32 LastStateUpdateTime;
    U32 MoveProgress;
    I32 LastMoveX;
    I32 LastMoveY;
    U32 LastMoveTime;
    BOOL StuckDetourActive;
    U32 StuckDetourCount;
    I32 StuckOriginalTargetX;
    I32 StuckOriginalTargetY;
    I32 StuckDetourTargetX;
    I32 StuckDetourTargetY;
    BOOL IsGridlocked;
    U32 GridlockLastUpdateTime;
    PATH_NODE* PathHead;
    PATH_NODE* PathTail;
    I32 PathTargetX;
    I32 PathTargetY;
    struct UNIT_STRUCT* Next;
};

struct PATH_NODE_STRUCT {
    POINT_2D Position;
    struct PATH_NODE_STRUCT* Next;
};

typedef struct {
    I32 Team;
    I32 Id;
} VISIBLE_ENTITY;

typedef struct {
    TEAM_RESOURCES Resources;
    BUILDING* Buildings;
    UNIT* Units;
    I32 AiAttitude;
    I32 AiMindset;
    char AiLastDecision[AI_LAST_DECISION_LEN];
    U32 AiLastUpdate;
    U32 AiLastClusterUpdate;
    U32 AiLastShuffleTime;
    MEMORY_CELL* MemoryMap;
    U8* VisibleNow;
    VISIBLE_ENTITY* VisibleEnemyUnits;
    I32 VisibleEnemyUnitCount;
    I32 VisibleEnemyUnitCapacity;
    VISIBLE_ENTITY* VisibleEnemyBuildings;
    I32 VisibleEnemyBuildingCount;
    I32 VisibleEnemyBuildingCapacity;
} TEAM_DATA;

typedef struct {
    I32 MapWidth;
    I32 MapHeight;
    I32 MapMaxDim;
    TERRAIN** Terrain;
    I32** PlasmaDensity;
    I32 TeamCount;
    TEAM_DATA TeamData[MAX_TEAMS];
    I32 NextUnitId;
    I32 NextBuildingId;
    I32 Difficulty;
    POINT_2D ViewportPos;
    U32 GameTime;
    U32 LastUpdate;
    U32 LastFogUpdate;
    I32 GameSpeed;
    BOOL IsPaused;
    BOOL IsGameOver;
    BOOL IsPlacingBuilding;
    I32 PendingBuildingTypeId;
    I32 PlacementX;
    I32 PlacementY;
    BOOL PlacingFromQueue;
    I32 PendingQueueIndex;
    BOOL IsRunning;
    UNIT* SelectedUnit;
    BUILDING* SelectedBuilding;
    BOOL ProductionMenuActive;
    I32 MenuPage;
    BOOL ShowGrid;
    BOOL ShowCoordinates;
    BOOL SeeEverything;
    BOOL GhostMode;
    BOOL FogDirty;
    BOOL IsCommandMode;
    I32 CommandType;
    I32 CommandX;
    I32 CommandY;
    U32 NoiseSeed;
    MEMORY_CELL* ScratchOccupancy;
    size_t ScratchOccupancyBytes;
    size_t TeamMemoryBytes;
    BOOL TeamDefeatedLogged[MAX_TEAMS];
} GAME_STATE;

typedef enum {
    MENU_MAIN,
    MENU_NEW_GAME,
    MENU_MANUAL,
    MENU_BUILD,
    MENU_UNITS,
    MENU_RESEARCH,
    MENU_SAVE,
    MENU_LOAD,
    MENU_IN_GAME,
    MENU_DEBUG,
    MENU_GAME_OVER
} MENU_TYPE;

typedef struct {
    MENU_TYPE CurrentMenu;
    I32 SelectedOption;
    I32 MenuPage;
    BOOL ExitRequested;
    I32 PrevMenu;
    I32 PendingMapWidth;
    I32 PendingMapHeight;
    I32 PendingDifficulty;
    I32 PendingTeamCount;
    char SaveFileName[NAME_MAX_LENGTH];
    char SavedGames[MAX_SAVED_GAMES][NAME_MAX_LENGTH];
    I32 SavedGameCount;
    I32 SelectedSaveIndex;
} MENU_STATE;

typedef struct {
    U32 ScreenWidth;
    U32 ScreenHeight;
    U32 MapViewHeight;
    U32 ViewportWidth;
    U32 ViewportHeight;
    char ViewBuffer[MAX_VIEWPORT_HEIGHT][MAX_VIEWPORT_WIDTH + 1];
    U8 ViewColors[MAX_VIEWPORT_HEIGHT][MAX_VIEWPORT_WIDTH];
    CONSOLE_BLIT_BUFFER ViewBlitInfo;
    char PrevViewBuffer[MAX_VIEWPORT_HEIGHT][MAX_VIEWPORT_WIDTH + 1];
    U8 PrevViewColors[MAX_VIEWPORT_HEIGHT][MAX_VIEWPORT_WIDTH];
    char PrevTopLine0[MAX_SCREEN_WIDTH + 1];
    char PrevTopLine1[MAX_SCREEN_WIDTH + 1];
    char PrevBottom[BOTTOM_BAR_HEIGHT][MAX_SCREEN_WIDTH + 1];
    char StatusLine[MAX_SCREEN_WIDTH + 1];
    char PrevStatusLine[MAX_SCREEN_WIDTH + 1];
    U32 StatusStartTime;
    BOOL BorderDrawn;
    BOOL MainMenuDrawn;
    I32 CachedNGWidth;
    I32 CachedNGHeight;
    I32 CachedNGDifficulty;
    I32 CachedNGTeams;
    I32 CachedNGSelection;
    I32 CachedLoadSelected;
    I32 CachedLoadCount;
    char CachedSaveName[NAME_MAX_LENGTH];
    BOOL DebugDrawn;
    char ScreenBuffer[MAX_SCREEN_HEIGHT][MAX_SCREEN_WIDTH + 1];
    char PrevScreenBuffer[MAX_SCREEN_HEIGHT][MAX_SCREEN_WIDTH + 1];
    U8 ScreenAttr[MAX_SCREEN_HEIGHT][MAX_SCREEN_WIDTH];
} RENDER_STATE;

typedef struct {
    I32 LastKeyVK;
    I32 LastKeyASCII;
    U32 LastKeyModifiers;
} INPUT_STATE;

typedef struct {
    GAME_STATE* GameState;
    MENU_STATE Menu;
    RENDER_STATE Render;
    INPUT_STATE Input;
} APP_STATE;

extern const BUILDING_TYPE BuildingTypes[];
extern const UNIT_TYPE UnitTypes[];
extern const U8 TeamColors[MAX_TEAMS];
extern APP_STATE App;
extern const char* TerminalTacticsManual;

U8 MakeAttr(U8 fore, U8 back);
I32 abs(I32 x);
U32 SimpleRandom(void);
float RandomFloat(void);
U32 RandomIndex(U32 max);
I32 GetTeamCountSafe(void);
I32 GetMaxUnitSight(void);
void CopyName(char* destination, const char* source);
const BUILDING_TYPE* GetBuildingTypeById(I32 typeId);
const UNIT_TYPE* GetUnitTypeById(I32 typeId);
BOOL IsValidTeam(I32 team);
BOOL HasTechLevel(I32 requiredLevel, I32 team);

#endif /* TT_TYPES_H */
