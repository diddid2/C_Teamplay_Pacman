#define _CRT_SECURE_NO_WARNINGS //abcd
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <wchar.h>

#define MAP_ROWS 24
#define MAP_COLS 48
#define SCREEN_ROWS (MAP_ROWS + 1)
#define SCREEN_COLS MAP_COLS

#define CH_WALL    L'■'
#define CH_COIN    L'·'
#define CH_EMPTY   L'　'
#define CH_GHOST   L'○'
#define CH_PLAYER  L'●'

typedef struct Player { int r, c, score, lives; } Player;
typedef struct Ghost {
    int r, c, sr, sc, dir, gtype;
    bool alive, vulnerable;
} Ghost;

bool coin[MAP_ROWS][MAP_COLS];
bool powerPellet[MAP_ROWS][MAP_COLS];
Player player;
Ghost ghosts[4];
bool globalVulnerable = false;
DWORD powerEndTime = 0;

CHAR_INFO* frameBuffer = NULL;
CHAR_INFO* prevFrameBuffer = NULL;
CHAR_INFO* tmpRowBuffer = NULL;

double playerMoveAcc = 0.0;
double playerSpeed = 8.0;
DWORD ghostMoveInterval = 300;

int currentDirR = 0, currentDirC = 0;

wchar_t* wcmap[MAP_ROWS] = {
    L"■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■",
    L"■○············································○■",
    L"■·■■■■·■■■■■■■·■■·■■■■■·■■■■■·■■■■■·■■·■■■■■■■·■",
    L"■·■■■■·■■■■■■■·■■·■■■■■·■■■■■·■■■■■·■■·■■■■■■■·■",
    L"■··············································■",
    L"■·■■■■·■■·■■■■■■■■■■■·■■·■■■■······■■■■■■·■■■■·■",
    L"■·■■■■·■■·■■■■■■■■■■■·■■·■■■■······■■■■■■·■■■■·■",
    L"■······■■···■■········■■·············■■········■",
    L"■■■■■■·■■·■■·■■·■■■■■·■■■■■■■■■■■■■■·■■·■■■■■■■■",
    L"■■■■■■·■■·■■·■■·■■■■■·■■■■■■■■■■■■■■·■■·■■■■■■■■",
    L"■························　　　　　　　　　·············■",
    L"■·■■■■·■■·■■■■■■■■■■■·■■·　　　　　　　　　■■·■■■■■■■■■·■",
    L"■·■■■■·■■·■■■■■■■■■■■·■■·　　　　　　　　　■■·■■■■■■■■■·■",
    L"■·■■■■·■■················　　　　　　　　　··········■■·■",
    L"■·■■■■·■■·■■■■■■■■■■·■■·■■■■■■■■■■·■■·■■■■■·■■·■",
    L"■·■■■■·■■·■■■■■■■■■■·■■·■■■■■■■■■■·■■·■■■■■·■■·■",
    L"■··········■■··································■",
    L"■■■■■■·■■■■■·■■·■■■■■·■■·■■■■■·■■■·■■·■■■■■·■■■■",
    L"■■■■■■·■■■■■·■■·■■■■■·■■·■■■■■·■■■·■■·■■■■■·■■■■",
    L"■······■■■■■·············■■■■■·■■■····■■■■·····■",
    L"■·■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■·■",
    L"■·■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■■■·■■■·■",
    L"■○······················●·····················○■",
    L"■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■"
};

void compose_frameBuffer_from_game_state();
void render_partial_updates(HANDLE hOut, bool forceFullWrite);

void setConsoleSizeAndFont() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    COORD bufferSize = { (SHORT)SCREEN_COLS, (SHORT)SCREEN_ROWS };
    SetConsoleScreenBufferSize(hOut, bufferSize);
    SMALL_RECT winRect = { 0, 0, (SHORT)(SCREEN_COLS - 1), (SHORT)(SCREEN_ROWS - 1) };
    SetConsoleWindowInfo(hOut, TRUE, &winRect);
    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(CONSOLE_FONT_INFOEX);
    GetCurrentConsoleFontEx(hOut, FALSE, &cfi);
    wcscpy_s(cfi.FaceName, LF_FACESIZE, L"Lucida Console");
    cfi.dwFontSize.X = 14;
    cfi.dwFontSize.Y = 28;
    cfi.FontWeight = FW_NORMAL;

    SetCurrentConsoleFontEx(hOut, FALSE, &cfi);
}


void hideCursor() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(hOut, &ci);
}

bool is_wall(int r, int c) {
    if (r < 0 || r >= MAP_ROWS || c < 0 || c >= MAP_COLS) return true;
    wchar_t ch = wcmap[r][c];
    return (ch == CH_WALL);
}
int manhattan(int a, int b, int c, int d) { return abs(a - c) + abs(b - d); }

// wcmap을 스캔하여 코인/플레이어/유령 배치
void init_world() {
    // 코인/파워 초기화
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            coin[r][c] = false;
            powerPellet[r][c] = false;
        }
    }

    // 기본값
    player.score = 0; player.lives = 3;
    player.r = 12; player.c = 49; // fallback(맵에 ● 없을 때)
    for (int i = 0; i < 4; i++) {
        ghosts[i].r = ghosts[i].sr = 0;
        ghosts[i].c = ghosts[i].sc = 0;
        ghosts[i].dir = i % 4;
        ghosts[i].gtype = i; // 0:Red,1:Orange,2:Blue,3:Pink
        ghosts[i].alive = true;
        ghosts[i].vulnerable = false;
    }
    int ghostCount = 0;

    // 맵 스캔
    for (int r = 0; r < MAP_ROWS; r++) {
        // 안전장치: wcmap[r]가 NULL이면 전부 벽 취급
        if (wcmap[r] == NULL) continue;
        for (int c = 0; c < MAP_COLS; c++) {
            wchar_t ch = wcmap[r][c];

            if (ch == CH_COIN) {
                coin[r][c] = true;
            }

            if (ch == CH_PLAYER) {
                player.r = r; player.c = c;
                //wcmap[r][c] = CH_EMPTY; // 스폰 심볼은 빈칸으로 치환
            }
            else if (ch == CH_GHOST && ghostCount < 4) {
                ghosts[ghostCount].r = ghosts[ghostCount].sr = r;
                ghosts[ghostCount].c = ghosts[ghostCount].sc = c;
                ghostCount++;
                //wcmap[r][c] = CH_EMPTY; // 스폰 심볼은 빈칸으로 치환
            }
        }
    }

    globalVulnerable = false;
    powerEndTime = 0;

    // 유령이 4마리 미만이면 적당한 빈칸에 보정 배치
    for (int i = ghostCount; i < 4; i++) {
        bool placed = false;
        for (int r = 1; r < MAP_ROWS - 1 && !placed; r++) {
            for (int c = 1; c < MAP_COLS - 1 && !placed; c++) {
                if (!is_wall(r, c)) {
                    ghosts[i].r = ghosts[i].sr = r;
                    ghosts[i].c = ghosts[i].sc = c;
                    placed = true;
                }
            }
        }
    }
}

// 화면 그리기
void compose_frameBuffer_from_game_state() {
    // 배경/타일
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            int idx = r * SCREEN_COLS + c;
            wchar_t ch = wcmap[r] ? wcmap[r][c] : CH_WALL;

            if (ch == CH_WALL) {
                frameBuffer[idx].Char.UnicodeChar = L'█';
                frameBuffer[idx].Attributes = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            }
            else {
                // 바닥
                frameBuffer[idx].Char.UnicodeChar = L' ';
                frameBuffer[idx].Attributes = 0;

                // 파워펠릿/코인
                if (powerPellet[r][c]) {
                    frameBuffer[idx].Char.UnicodeChar = L'＠';
                    frameBuffer[idx].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                }
                else if (coin[r][c]) {
                    frameBuffer[idx].Char.UnicodeChar = L'·';
                    frameBuffer[idx].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                }
            }
        }
    }

    // UI 라인 초기화
    int uiRow = SCREEN_ROWS - 1;
    for (int c = 0; c < SCREEN_COLS; c++) {
        int idx = uiRow * SCREEN_COLS + c;
        frameBuffer[idx].Char.UnicodeChar = L' ';
        frameBuffer[idx].Attributes = 0;
    }

    // 유령
    for (int i = 0; i < 4; i++) {
        if (!ghosts[i].alive) continue;
        int gr = ghosts[i].r, gc = ghosts[i].c;
        if (gr < 0 || gr >= MAP_ROWS || gc < 0 || gc >= MAP_COLS) continue;
        int idx = gr * SCREEN_COLS + gc;
        if (ghosts[i].vulnerable || globalVulnerable) {
            frameBuffer[idx].Char.UnicodeChar = L'V';
            frameBuffer[idx].Attributes = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        }
        else {
            frameBuffer[idx].Char.UnicodeChar = L'G';
            WORD attr =
                (i == 0 ? (FOREGROUND_RED | FOREGROUND_INTENSITY) :
                    i == 1 ? (FOREGROUND_GREEN | FOREGROUND_INTENSITY) :
                    i == 2 ? (FOREGROUND_BLUE | FOREGROUND_INTENSITY) :
                    (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY));
            frameBuffer[idx].Attributes = attr;
        }
    }

    // 플레이어
    if (player.r >= 0 && player.r < MAP_ROWS && player.c >= 0 && player.c < MAP_COLS) {
        int idx = player.r * SCREEN_COLS + player.c;
        frameBuffer[idx].Char.UnicodeChar = L'C';
        frameBuffer[idx].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    }

    // UI 텍스트
    wchar_t ui[128];
    swprintf(ui, 128, L" SCORE: %d   LIVES: %d ", player.score, player.lives);
    int startCol = 0; int uiRowIdx = (SCREEN_ROWS - 1) * SCREEN_COLS + startCol;
    for (int i = 0; ui[i] && startCol + i < SCREEN_COLS; i++) {
        frameBuffer[uiRowIdx + i].Char.UnicodeChar = ui[i];
        frameBuffer[uiRowIdx + i].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    }
}

void render_partial_updates(HANDLE hOut, bool forceFullWrite) {
    COORD bufSize = { (SHORT)SCREEN_COLS, (SHORT)SCREEN_ROWS };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRegion;

    if (forceFullWrite) {
        writeRegion.Left = 0; writeRegion.Top = 0; writeRegion.Right = SCREEN_COLS - 1; writeRegion.Bottom = SCREEN_ROWS - 1;
        WriteConsoleOutputW(hOut, frameBuffer, bufSize, bufCoord, &writeRegion);
        memcpy(prevFrameBuffer, frameBuffer, sizeof(CHAR_INFO) * SCREEN_ROWS * SCREEN_COLS);
        return;
    }

    for (int r = 0; r < SCREEN_ROWS; r++) {
        int c = 0;
        while (c < SCREEN_COLS) {
            int idx = r * SCREEN_COLS + c;
            if (frameBuffer[idx].Char.UnicodeChar == prevFrameBuffer[idx].Char.UnicodeChar &&
                frameBuffer[idx].Attributes == prevFrameBuffer[idx].Attributes) {
                c++; continue;
            }
            int startC = c; int runLen = 1;
            while (startC + runLen < SCREEN_COLS) {
                int j = r * SCREEN_COLS + startC + runLen;
                if (frameBuffer[j].Char.UnicodeChar != prevFrameBuffer[j].Char.UnicodeChar ||
                    frameBuffer[j].Attributes != prevFrameBuffer[j].Attributes) runLen++; else break;
            }
            for (int k = 0; k < runLen; k++) tmpRowBuffer[k] = frameBuffer[r * SCREEN_COLS + startC + k];
            writeRegion.Left = (SHORT)startC; writeRegion.Top = (SHORT)r; writeRegion.Right = (SHORT)(startC + runLen - 1); writeRegion.Bottom = (SHORT)r;
            COORD writeBufSize = { (SHORT)runLen, 1 };
            COORD writeBufCoord = { 0, 0 };
            WriteConsoleOutputW(hOut, tmpRowBuffer, writeBufSize, writeBufCoord, &writeRegion);
            for (int k = 0; k < runLen; k++) prevFrameBuffer[r * SCREEN_COLS + startC + k] = tmpRowBuffer[k];
            c = startC + runLen;
        }
    }
}

void move_ghost(Ghost* g) {
    if (!g->alive) { g->r = g->sr; g->c = g->sc; g->alive = true; g->vulnerable = false; return; }
    int dr[4] = { -1,0,1,0 }; int dc[4] = { 0,1,0,-1 };
    int tryOrder[5]; tryOrder[0] = g->dir;
    for (int i = 1; i < 5; i++) tryOrder[i] = rand() % 4;
    for (int i = 0; i < 5; i++) {
        int d = tryOrder[i]; int nr = g->r + dr[d], nc = g->c + dc[d];
        if (nr < 0 || nr >= MAP_ROWS || nc < 0 || nc >= MAP_COLS) continue;
        if (!is_wall(nr, nc)) { g->r = nr; g->c = nc; g->dir = d; return; }
    }
}

void check_collect_and_collision() {
    if (powerPellet[player.r][player.c]) {
        powerPellet[player.r][player.c] = false;
        player.score += 50;
        globalVulnerable = true;
        powerEndTime = GetTickCount() + 8000;
        for (int i = 0; i < 4; i++) ghosts[i].vulnerable = true;
    }
    else if (coin[player.r][player.c]) {
        coin[player.r][player.c] = false;
        player.score += 10;
    }

    for (int i = 0; i < 4; i++) {
        Ghost* g = &ghosts[i];
        if (!g->alive) continue;
        if (g->r == player.r && g->c == player.c) {
            if (g->vulnerable || globalVulnerable) {
                player.score += 200; g->alive = false;
            }
            else {
                player.lives--;
                int bestR = -1, bestC = -1, bestDist = -1;
                for (int r = 0; r < MAP_ROWS; r++) {
                    for (int c = 0; c < MAP_COLS; c++) {
                        if (is_wall(r, c)) continue;
                        int mind = 100000; for (int gg = 0; gg < 4; gg++) {
                            int d = manhattan(r, c, ghosts[gg].sr, ghosts[gg].sc);
                            if (d < mind) mind = d;
                        }
                        if (mind > bestDist) { bestDist = mind; bestR = r; bestC = c; }
                    }
                }
                if (bestR >= 0) { player.r = bestR; player.c = bestC; }
                else { player.r = 12; player.c = 49; }
                for (int j = 0; j < 4; j++) {
                    ghosts[j].r = ghosts[j].sr; ghosts[j].c = ghosts[j].sc;
                    ghosts[j].alive = true; ghosts[j].vulnerable = false;
                }
                Sleep(600);
                return;
            }
        }
    }
}

int main() {
    srand((unsigned int)time(NULL));
    setConsoleSizeAndFont(); hideCursor();

    frameBuffer = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * SCREEN_ROWS * SCREEN_COLS);
    prevFrameBuffer = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * SCREEN_ROWS * SCREEN_COLS);
    tmpRowBuffer = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * SCREEN_COLS);
    if (!frameBuffer || !prevFrameBuffer || !tmpRowBuffer) { fprintf(stderr, "메모리 할당 실패\n"); return 1; }
    for (int i = 0; i < SCREEN_ROWS * SCREEN_COLS; i++) { prevFrameBuffer[i].Char.UnicodeChar = 0xFFFF; prevFrameBuffer[i].Attributes = 0xFFFF; }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    for (int i = 0; i < SCREEN_ROWS * SCREEN_COLS; i++) { frameBuffer[i].Char.UnicodeChar = L' '; frameBuffer[i].Attributes = 0; }
    wchar_t* intro1 = L"==================================";
    wchar_t* intro2 = L"===          Pac-Man!          ===";
    wchar_t* intro3 = L"엔터 키를 누르면 게임이 시작됩니다";
    int r0 = 10, c0 = 33;
    for (int i = 0; intro1[i]; i++) frameBuffer[(r0)*SCREEN_COLS + c0 + i].Char.UnicodeChar = intro1[i];
    for (int i = 0; intro2[i]; i++) frameBuffer[(r0 + 1) * SCREEN_COLS + c0 + i].Char.UnicodeChar = intro2[i];
    for (int i = 0; intro3[i]; i++) frameBuffer[(r0 + 2) * SCREEN_COLS + c0 + i].Char.UnicodeChar = intro3[i];
    for (int i = 0; intro1[i]; i++) frameBuffer[(r0 + 3) * SCREEN_COLS + c0 + i].Char.UnicodeChar = intro1[i];
    COORD bufSize = { (SHORT)SCREEN_COLS, (SHORT)SCREEN_ROWS };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, 0, (SHORT)(SCREEN_COLS - 1), (SHORT)(SCREEN_ROWS - 1) };
    WriteConsoleOutputW(hOut, frameBuffer, bufSize, bufCoord, &writeRegion);
    while (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) Sleep(40);

    init_world();
    compose_frameBuffer_from_game_state();
    render_partial_updates(hOut, true);

    LARGE_INTEGER freq, lastTime, curTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    DWORD lastGhostMoveTick = GetTickCount();
    DWORD lastRenderTick = GetTickCount();
    const DWORD renderIntervalMs = 16;

    bool running = true;
    while (running) {
        QueryPerformanceCounter(&curTime);
        double dt = (double)(curTime.QuadPart - lastTime.QuadPart) / (double)freq.QuadPart;
        lastTime = curTime;

        bool anyDirKey = false;
        int newDirR = currentDirR, newDirC = currentDirC;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) { newDirR = 0; newDirC = -1; anyDirKey = true; }
        else if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) { newDirR = 0; newDirC = 1; anyDirKey = true; }
        if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) { newDirR = -1; newDirC = 0; anyDirKey = true; }
        else if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) { newDirR = 1; newDirC = 0; anyDirKey = true; }

        if (anyDirKey) {
            if (newDirR != currentDirR || newDirC != currentDirC) {
                currentDirR = newDirR; currentDirC = newDirC;
                playerMoveAcc = 0.0;
            }
            else {
                currentDirR = newDirR; currentDirC = newDirC;
            }
        }

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) break;

        if (currentDirR != 0 || currentDirC != 0) {
            int nextR = player.r + currentDirR;
            int nextC = player.c + currentDirC;
            if (is_wall(nextR, nextC)) {
                playerMoveAcc += dt * playerSpeed;
                if (playerMoveAcc > 0.5) playerMoveAcc = 0.5;
            }
            else {
                playerMoveAcc += dt * playerSpeed;
                while (playerMoveAcc >= 1.0) {
                    playerMoveAcc -= 1.0;
                    int targetR = player.r + currentDirR;
                    int targetC = player.c + currentDirC;
                    if (!is_wall(targetR, targetC)) {
                        player.r = targetR; player.c = targetC;
                        check_collect_and_collision();
                    }
                    else {
                        break;
                    }
                }
            }
        }

        DWORD nowTick = GetTickCount();
        if (nowTick - lastGhostMoveTick >= ghostMoveInterval) {
            for (int i = 0; i < 4; i++) move_ghost(&ghosts[i]);
            check_collect_and_collision();
            lastGhostMoveTick = nowTick;
        }

        if (globalVulnerable && GetTickCount() >= powerEndTime) {
            globalVulnerable = false;
            for (int i = 0; i < 4; i++) ghosts[i].vulnerable = false;
        }

        if (nowTick - lastRenderTick >= renderIntervalMs) {
            compose_frameBuffer_from_game_state();
            bool forceFull = (prevFrameBuffer[0].Char.UnicodeChar == 0xFFFF);
            render_partial_updates(hOut, forceFull);
            lastRenderTick = nowTick;
        }

        int remain = 0;
        for (int r = 0; r < MAP_ROWS; r++)
            for (int c = 0; c < MAP_COLS; c++)
                if (coin[r][c] || powerPellet[r][c]) remain++;

        if (remain == 0) {
            for (int i = 0; i < SCREEN_ROWS * SCREEN_COLS; i++) { frameBuffer[i].Char.UnicodeChar = L' '; frameBuffer[i].Attributes = 0; }
            wchar_t msg1[128]; swprintf(msg1, 128, L"!! CONGRATS !!  ALL COLLECTED");
            wchar_t msg2[128]; swprintf(msg2, 128, L"FINAL SCORE: %d", player.score);
            int rr = 10, cc = 30;
            for (int i = 0; msg1[i]; i++) frameBuffer[(rr)*SCREEN_COLS + cc + i].Char.UnicodeChar = msg1[i];
            for (int i = 0; msg2[i]; i++) frameBuffer[(rr + 2) * SCREEN_COLS + cc + i].Char.UnicodeChar = msg2[i];
            WriteConsoleOutputW(hOut, frameBuffer, bufSize, bufCoord, &writeRegion);
            while (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) Sleep(50);
            break;
        }
        if (player.lives <= 0) {
            for (int i = 0; i < SCREEN_ROWS * SCREEN_COLS; i++) { frameBuffer[i].Char.UnicodeChar = L' '; frameBuffer[i].Attributes = 0; }
            wchar_t g1[128] = L"======== 게임 오버 ========";
            wchar_t g2[128]; swprintf(g2, 128, L"FINAL SCORE: %d", player.score);
            int rr = 10, cc = 30;
            for (int i = 0; g1[i]; i++) frameBuffer[(rr)*SCREEN_COLS + cc + i].Char.UnicodeChar = g1[i];
            for (int i = 0; g2[i]; i++) frameBuffer[(rr + 2) * SCREEN_COLS + cc + i].Char.UnicodeChar = g2[i];
            WriteConsoleOutputW(hOut, frameBuffer, bufSize, bufCoord, &writeRegion);
            while (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) Sleep(50);
            break;
        }

        Sleep(1);
    }

    free(frameBuffer); free(prevFrameBuffer); free(tmpRowBuffer);
    HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleActiveScreenBuffer(stdOut);
    system("cls");
    return 0;
}
