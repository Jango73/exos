
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


    TicTacToe - Full-screen interactive tic-tac-toe game

\************************************************************************/

#include "../../../runtime/include/exos-runtime.h"
#include "../../../runtime/include/exos.h"
#include "../../../kernel/include/input/VKey.h"

/************************************************************************/

typedef struct {
    int board[3][3];
    int cursorX;
    int cursorY;
    int playerWins;
    int computerWins;
    int ties;
    int gamesPlayed;
} GAME_STATE;

volatile GAME_STATE gameState = {
    .cursorX = 0,
    .cursorY = 0,
    .playerWins = 0,
    .computerWins = 0,
    .ties = 0,
    .gamesPlayed = 0
};

/************************************************************************/

void GotoCursor(int x, int y) {
    POINT pos;
    pos.X = x;
    pos.Y = y;
    ConsoleGotoXY(&pos);
}

/************************************************************************/

void InitializeBoard(void) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            gameState.board[i][j] = ' ';
        }
    }
}

/************************************************************************/

void DisplayFullScreen(void) {
    int i, j;

    ConsoleClear();

    GotoCursor(0, 0);
    printf("==============================================================================");
    GotoCursor(0, 1);
    printf("                                 TIC-TAC-TOE                                  ");
    GotoCursor(0, 2);
    printf("==============================================================================");

    GotoCursor(0, 4);
    printf("Games Played: %d  |  Player Wins: %d  |  Computer Wins: %d  |  Ties: %d",
           gameState.gamesPlayed, gameState.playerWins, gameState.computerWins, gameState.ties);

    GotoCursor(0, 6);
    printf("                          Use Arrow Keys to Move");
    GotoCursor(0, 7);
    printf("                        ENTER to Place X  |  ESC to Quit");

    GotoCursor(0, 9);
    printf("                               0   1   2");
    GotoCursor(0, 10);
    printf("                           +---+---+---+");

    for (i = 0; i < 3; i++) {
        GotoCursor(0, 11 + i * 2);
        printf("                       %d   |", (I32)i);

        for (j = 0; j < 3; j++) {
            if (i == gameState.cursorY && j == gameState.cursorX) {
                printf("[%c]", gameState.board[i][j] == ' ' ? (I32)'?' : (I32)gameState.board[i][j]);
            } else {
                printf(" %c ", (I32)gameState.board[i][j]);
            }
            if (j < 2) printf("|");
        }

        printf("|");
        if (i < 2) {
            GotoCursor(0, 12 + i * 2);
            printf("                           +---+---+---+");
        }
    }

    GotoCursor(0, 17);
    printf("                           +---+---+---+");

    if (gameState.gamesPlayed > 0) {
        float winRate = (float)gameState.playerWins / (float)gameState.gamesPlayed * 100.0f;
        GotoCursor(0, 19);
        printf("                      Player Win Rate: %.1f%%", winRate);
    }

    GotoCursor(0, 21);
    printf("==============================================================================");
}

/************************************************************************/

int CheckWin(char player) {
    for (int i = 0; i < 3; i++) {
        if (gameState.board[i][0] == player && gameState.board[i][1] == player && gameState.board[i][2] == player)
            return 1;
        if (gameState.board[0][i] == player && gameState.board[1][i] == player && gameState.board[2][i] == player)
            return 1;
    }
    if (gameState.board[0][0] == player && gameState.board[1][1] == player && gameState.board[2][2] == player)
        return 1;
    if (gameState.board[0][2] == player && gameState.board[1][1] == player && gameState.board[2][0] == player)
        return 1;
    return 0;
}

/************************************************************************/

int IsBoardFull(void) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (gameState.board[i][j] == ' ')
                return 0;
        }
    }
    return 1;
}

/************************************************************************/

int EvaluateBoard(void) {
    if (CheckWin('X')) return 10;
    if (CheckWin('O')) return -10;
    return 0;
}

/************************************************************************/

int Minimax(int depth, int isMaximizing) {
    int score = EvaluateBoard();

    if (score == 10) return score - depth;
    if (score == -10) return score + depth;
    if (IsBoardFull()) return 0;

    if (isMaximizing) {
        int best = -1000;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if (gameState.board[i][j] == ' ') {
                    gameState.board[i][j] = 'X';
                    int score = Minimax(depth + 1, 0);
                    best = (best > score) ? best : score;
                    gameState.board[i][j] = ' ';
                }
            }
        }
        return best;
    } else {
        int best = 1000;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if (gameState.board[i][j] == ' ') {
                    gameState.board[i][j] = 'O';
                    int score = Minimax(depth + 1, 1);
                    best = (best < score) ? best : score;
                    gameState.board[i][j] = ' ';
                }
            }
        }
        return best;
    }
}

/************************************************************************/

void ComputerMove(void) {
    int bestScore = 1000;
    int bestRow = -1;
    int bestCol = -1;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (gameState.board[i][j] == ' ') {
                gameState.board[i][j] = 'O';
                int score = Minimax(0, 1);
                gameState.board[i][j] = ' ';

                if (score < bestScore) {
                    bestScore = score;
                    bestRow = i;
                    bestCol = j;
                }
            }
        }
    }

    if (bestRow != -1 && bestCol != -1) {
        gameState.board[bestRow][bestCol] = 'O';
    }
}

/************************************************************************/

void ShowGameResult(char* message) {
    DisplayFullScreen();
    GotoCursor(30, 20);
    printf("%s", message);
    GotoCursor(25, 22);
    printf("Press any key to continue...");
    getch();
}

/************************************************************************/

int PlayGame(void) {
    InitializeBoard();
    gameState.cursorX = 1;
    gameState.cursorY = 1;

    while(1) {
        DisplayFullScreen();

        int key = getkey();

        switch (key) {
            case VK_ESCAPE:
                return 0;

            case VK_UP:
                gameState.cursorY = (gameState.cursorY > 0) ? gameState.cursorY - 1 : 2;
                break;

            case VK_DOWN:
                gameState.cursorY = (gameState.cursorY < 2) ? gameState.cursorY + 1 : 0;
                break;

            case VK_LEFT:
                gameState.cursorX = (gameState.cursorX > 0) ? gameState.cursorX - 1 : 2;
                break;

            case VK_RIGHT:
                gameState.cursorX = (gameState.cursorX < 2) ? gameState.cursorX + 1 : 0;
                break;

            case VK_ENTER:
                if (gameState.board[gameState.cursorY][gameState.cursorX] == ' ') {
                    gameState.board[gameState.cursorY][gameState.cursorX] = 'X';

                    if (CheckWin('X')) {
                        gameState.gamesPlayed++;
                        gameState.playerWins++;
                        ShowGameResult("CONGRATULATIONS! You won!");
                        return 1;
                    }

                    if (IsBoardFull()) {
                        gameState.gamesPlayed++;
                        gameState.ties++;
                        ShowGameResult("It's a tie!");
                        return 1;
                    }

                    ComputerMove();

                    if (CheckWin('O')) {
                        gameState.gamesPlayed++;
                        gameState.computerWins++;
                        ShowGameResult("Computer wins! Better luck next time.");
                        return 1;
                    }

                    if (IsBoardFull()) {
                        gameState.gamesPlayed++;
                        gameState.ties++;
                        ShowGameResult("It's a tie!");
                        return 1;
                    }
                }
                break;
        }
    }

    return 1;
}

/************************************************************************/

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    ConsoleClear();

    while(1) {
        if (!PlayGame()) {
            ConsoleClear();
            GotoCursor(0, 0);
            printf("Thanks for playing Tic-Tac-Toe!");
            GotoCursor(0, 1);
            printf("Final Stats - Games: %d, Player Wins: %d, Computer Wins: %d, Ties: %d\n",
                   gameState.gamesPlayed, gameState.playerWins, gameState.computerWins, gameState.ties);
            break;
        }
    }

    return 0;
}
