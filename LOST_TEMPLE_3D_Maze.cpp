#include <GL/glut.h>
#include <GL/glu.h>
#include <windows.h> // Shift 키의 현재 눌림 상태를 확인하기 위해 사용
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ============================================================
// LOST TEMPLE - 3D 미로 게임
// Visual Studio + FreeGLUT / OpenGL 고정 기능 파이프라인
//
// 조작 방법:
//   마우스          : 카메라 시점 회전
//   W / S           : 전진 / 후진
//   A / D           : 왼쪽 / 오른쪽 이동
//   Shift           : 누르고 있는 동안 달리기
//   R               : 게임 재시작
//   ESC             : 게임 종료
//
// 2D 버전에서 사용한 15x15 문자 미로 구조를 재사용한다.
// '#' = 벽, '.' = 바닥, S = 시작 지점, K = 열쇠,
// V = SOR 방식의 꽃병, C = SOR 방식의 기둥, E = 출구.
// ============================================================

const int ROWS = 15;
const int COLS = 15;

const char* maze[ROWS] = {
    "###############",
    "#S#.....#.....#",
    "#.#.###.#.#.###",
    "#.#K..#...#...#",
    "#.###.#######.#",
    "#.#...#.......#",
    "#.#####.#####.#",
    "#.#.....#.....#",
    "#.#.#####.#####",
    "#...#...#.#V..#",
    "#####.###.###.#",
    "#.....#...#...#",
    "#.###.#.###.#.#",
    "#..C#.......#E#",
    "###############"
};

const float CELL = 2.0f;
const float WALL_HEIGHT = 2.6f;
const float PLAYER_RADIUS = 0.22f;
const float MOVE_SPEED = 1.8f; // 일반 이동 속도 (월드 좌표 단위/초)
const float RUN_SPEED = 3.5f;  // Shift를 누르고 있을 때의 달리기 속도
const float TURN_STEP = 0.075f;
const float PI = 3.14159265358979323846f;

float playerX = 3.0f;
float playerZ = 3.0f;
float playerAngle = 0.0f; // 수평 시선 방향이며 0은 +Z 방향이다.
float cameraPitch = 0.0f; // 수직 시선 각도
float mouseSensitivity = 0.0035f;

int windowWidth = 1000;
int windowHeight = 700;
bool ignoreNextMouseMove = true;

bool hasKey = false;
bool gameWon = false;

// 키보드 입력 상태: 여러 키를 동시에 누른 상태로 유지할 수 있다.
bool keyW = false;
bool keyA = false;
bool keyS = false;
bool keyD = false;
int lastMovementTime = 0;

// ------------------------------------------------------------
// 재질, 정육면체, 글자 출력을 위한 보조 함수
// ------------------------------------------------------------
void setMaterial(float r, float g, float b, float shine = 12.0f)
{
    GLfloat color[] = { r, g, b, 1.0f };
    GLfloat spec[] = { 0.18f, 0.18f, 0.18f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shine);
}

void drawCube(float x, float y, float z,
    float sx, float sy, float sz,
    float r, float g, float b)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    setMaterial(r, g, b);
    glutSolidCube(1.0f);
    glPopMatrix();
}

void drawText(float x, float y, const char* text)
{
    glRasterPos2f(x, y);
    for (const char* p = text; *p; ++p)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);
}

// ------------------------------------------------------------
// SOR 방식의 회전 곡면 생성
// 단면 좌표는 (반지름, 높이)로 구성한다. 단면을 회전시켜
// Y축을 중심으로 회전시켜 회전 대칭 형태의 메시를 만든다.
// ------------------------------------------------------------
void drawSOR(const float profile[][2], int profileCount,
    float red, float green, float blue)
{
    const int slices = 32;
    setMaterial(red, green, blue, 28.0f);

    for (int i = 0; i < profileCount - 1; ++i)
    {
        glBegin(GL_QUAD_STRIP);

        for (int j = 0; j <= slices; ++j)
        {
            float angle = 2.0f * PI * (float)j / (float)slices;
            float c = std::cos(angle);
            float s = std::sin(angle);

            float r0 = profile[i][0];
            float y0 = profile[i][1];
            float r1 = profile[i + 1][0];
            float y1 = profile[i + 1][1];

            glNormal3f(c, 0.0f, s);
            glVertex3f(r0 * c, y0, r0 * s);

            glNormal3f(c, 0.0f, s);
            glVertex3f(r1 * c, y1, r1 * s);
        }

        glEnd();
    }
}

void drawVase()
{
    // 단순한 꽃병 단면: 바닥 -> 몸통 -> 목 -> 입구 순서로 구성한다.
    const float profile[][2] = {
        {0.00f, 0.05f}, {0.28f, 0.05f}, {0.38f, 0.18f},
        {0.48f, 0.48f}, {0.50f, 0.78f}, {0.40f, 1.02f},
        {0.25f, 1.18f}, {0.22f, 1.42f}, {0.30f, 1.48f},
        {0.30f, 1.55f}, {0.20f, 1.55f}
    };

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 0.0f);
    drawSOR(profile, (int)(sizeof(profile) / sizeof(profile[0])),
        0.55f, 0.20f, 0.10f);
    glPopMatrix();
}

void drawColumn()
{
    // 아래쪽 받침과 위쪽 머리 부분이 넓은 기둥 단면을 구성한다.
    const float profile[][2] = {
        {0.00f, 0.05f}, {0.42f, 0.05f}, {0.42f, 0.18f},
        {0.30f, 0.28f}, {0.27f, 0.38f}, {0.27f, 1.85f},
        {0.32f, 1.98f}, {0.48f, 2.08f}, {0.48f, 2.22f},
        {0.00f, 2.22f}
    };

    drawSOR(profile, (int)(sizeof(profile) / sizeof(profile[0])),
        0.68f, 0.66f, 0.58f);
}

void drawKeyObject()
{
    // 열쇠를 장식용으로 표시한다. 실제 획득은 플레이어와의 거리를 기준으로 판정한다.
    setMaterial(1.0f, 0.72f, 0.05f, 70.0f);
    glPushMatrix();
    glTranslatef(0.0f, 0.55f, 0.0f);
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glutSolidTorus(0.07, 0.20, 12, 24);
    glPopMatrix();

    drawCube(0.23f, 0.55f, 0.0f, 0.42f, 0.10f, 0.10f,
        1.0f, 0.72f, 0.05f);
    drawCube(0.38f, 0.48f, 0.0f, 0.10f, 0.18f, 0.10f,
        1.0f, 0.72f, 0.05f);
}

void drawExitMarker(bool unlocked)
{
    float r = unlocked ? 0.10f : 0.65f;
    float g = unlocked ? 0.85f : 0.12f;
    float b = unlocked ? 0.25f : 0.10f;

    // 양쪽 기둥과 위쪽 가로대를 배치해 출구 문을 표현한다.
    drawCube(0.25f, 1.05f, 0.0f, 0.18f, 2.1f, 0.18f, r, g, b);
    drawCube(1.75f, 1.05f, 0.0f, 0.18f, 2.1f, 0.18f, r, g, b);
    drawCube(1.0f, 2.05f, 0.0f, 1.65f, 0.18f, 0.18f, r, g, b);
}

// ------------------------------------------------------------
// 미로 구성: 바닥, 벽, SOR 오브젝트, 열쇠, 출구 배치
// ------------------------------------------------------------
void drawFloorTile(float x, float z, int row, int col)
{
    // 타일 구분이 잘 되도록 돌바닥의 색상을 조금씩 다르게 표현한다.
    float shade = ((row + col) % 2 == 0) ? 0.42f : 0.36f;

    setMaterial(shade, shade * 0.94f, shade * 0.84f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(x, 0.0f, z);
    glVertex3f(x + CELL, 0.0f, z);
    glVertex3f(x + CELL, 0.0f, z + CELL);
    glVertex3f(x, 0.0f, z + CELL);
    glEnd();
}

void drawWall(float x, float z)
{
    // 벽 큐브의 중심을 바닥에서 벽 높이의 절반만큼 올린다.
    drawCube(x + CELL * 0.5f, WALL_HEIGHT * 0.5f,
        z + CELL * 0.5f,
        CELL, WALL_HEIGHT, CELL,
        0.43f, 0.42f, 0.39f);

    // 벽 위쪽에 어두운 띠를 추가해 돌벽의 세부 표현을 더한다.
    drawCube(x + CELL * 0.5f, WALL_HEIGHT - 0.12f,
        z + CELL * 0.5f,
        CELL * 0.98f, 0.12f, CELL * 0.98f,
        0.30f, 0.29f, 0.27f);
}

void drawMaze()
{
    for (int row = 0; row < ROWS; ++row)
    {
        for (int col = 0; col < COLS; ++col)
        {
            float x = col * CELL;
            float z = row * CELL;
            char tile = maze[row][col];

            drawFloorTile(x, z, row, col);

            if (tile == '#')
            {
                drawWall(x, z);
            }
            else if (tile == 'K' && !hasKey)
            {
                glPushMatrix();
                glTranslatef(x + CELL * 0.5f, 0.0f,
                    z + CELL * 0.5f);
                drawKeyObject();
                glPopMatrix();
            }
            else if (tile == 'V')
            {
                glPushMatrix();
                glTranslatef(x + CELL * 0.5f, 0.0f,
                    z + CELL * 0.5f);
                drawVase();
                glPopMatrix();
            }
            else if (tile == 'C')
            {
                glPushMatrix();
                glTranslatef(x + CELL * 0.5f, 0.0f,
                    z + CELL * 0.5f);
                drawColumn();
                glPopMatrix();
            }
            else if (tile == 'E')
            {
                glPushMatrix();
                glTranslatef(x, 0.0f, z + CELL * 0.5f);
                drawExitMarker(hasKey);
                glPopMatrix();
            }
        }
    }
}

// ------------------------------------------------------------
// 플레이어 이동 및 벽 충돌 처리
// ------------------------------------------------------------
// 원형 플레이어와 벽의 충돌을 검사한다. 플레이어는 XZ 평면 위의 원으로 간주하고,
// 각 벽 타일은 축에 평행한 정사각형으로 간주한다.
bool canPlayerOccupy(float x, float z)
{
    if (x - PLAYER_RADIUS < 0.0f || z - PLAYER_RADIUS < 0.0f ||
        x + PLAYER_RADIUS >= COLS * CELL ||
        z + PLAYER_RADIUS >= ROWS * CELL)
        return false;

    int minCol = (int)std::floor((x - PLAYER_RADIUS) / CELL);
    int maxCol = (int)std::floor((x + PLAYER_RADIUS) / CELL);
    int minRow = (int)std::floor((z - PLAYER_RADIUS) / CELL);
    int maxRow = (int)std::floor((z + PLAYER_RADIUS) / CELL);

    for (int row = minRow; row <= maxRow; ++row)
    {
        for (int col = minCol; col <= maxCol; ++col)
        {
            if (row < 0 || row >= ROWS || col < 0 || col >= COLS)
                return false;
            if (maze[row][col] != '#')
                continue;

            float left = col * CELL;
            float right = left + CELL;
            float top = row * CELL;
            float bottom = top + CELL;

            float closestX = (x < left) ? left : ((x > right) ? right : x);
            float closestZ = (z < top) ? top : ((z > bottom) ? bottom : z);
            float dx = x - closestX;
            float dz = z - closestZ;

            if (dx * dx + dz * dz < PLAYER_RADIUS * PLAYER_RADIUS)
                return false;
        }
    }
    return true;
}

void checkPickupsAndExit()
{
    int col = (int)std::floor(playerX / CELL);
    int row = (int)std::floor(playerZ / CELL);

    if (row < 0 || row >= ROWS || col < 0 || col >= COLS)
        return;

    if (maze[row][col] == 'K' && !hasKey)
    {
        hasKey = true;
        glutPostRedisplay();
    }

    if (maze[row][col] == 'E' && hasKey)
    {
        gameWon = true;
        glutPostRedisplay();
    }
}

void movePlayer(float forwardAmount, float strafeAmount, float deltaTime)
{
    if (gameWon) return;

    float forwardX = std::sin(playerAngle);
    float forwardZ = std::cos(playerAngle);
    float rightX = std::cos(playerAngle);
    float rightZ = -std::sin(playerAngle);

    // 대각선 이동이 직선 이동보다 빨라지지 않도록 입력 방향을 정규화한다.
    float inputLength = std::sqrt(forwardAmount * forwardAmount +
        strafeAmount * strafeAmount);
    if (inputLength > 1.0f)
    {
        forwardAmount /= inputLength;
        strafeAmount /= inputLength;
    }

    // Shift 키가 눌려 있으면 달리기 속도, 아니면 일반 이동 속도를 사용한다.
    // GetAsyncKeyState는 키를 누르고 있는 동안 계속 눌림 상태를 반환한다.
    float currentSpeed = (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        ? RUN_SPEED
        : MOVE_SPEED;

    // 카메라가 바라보는 방향과 좌우 방향을 조합해 실제 이동량을 계산한다.
    float dx = (forwardX * forwardAmount + rightX * strafeAmount)
        * currentSpeed * deltaTime;
    float dz = (forwardZ * forwardAmount + rightZ * strafeAmount)
        * currentSpeed * deltaTime;

    // 한 번의 이동 거리를 작은 단계로 나눈다. 이를 통해 벽 모서리를 통과하는 현상을
    // 방지하고 벽면을 따라 부드럽게 미끄러지듯 이동할 수 있게 한다.
    float maxStep = PLAYER_RADIUS * 0.35f;
    int steps = (int)std::ceil(std::sqrt(dx * dx + dz * dz) / maxStep);
    if (steps < 1) steps = 1;
    float stepX = dx / (float)steps;
    float stepZ = dz / (float)steps;

    for (int i = 0; i < steps; ++i)
    {
        // X축과 Z축의 충돌을 각각 처리해 벽을 따라 이동할 수 있게 한다.
        float candidateX = playerX + stepX;
        if (canPlayerOccupy(candidateX, playerZ))
            playerX = candidateX;

        float candidateZ = playerZ + stepZ;
        if (canPlayerOccupy(playerX, candidateZ))
            playerZ = candidateZ;
    }

    checkPickupsAndExit();
    glutPostRedisplay();
}

void restartGame()
{
    playerX = 3.0f;
    playerZ = 3.0f;
    playerAngle = 0.0f;
    cameraPitch = 0.0f;
    hasKey = false;
    gameWon = false;
    keyW = keyA = keyS = keyD = false;
    ignoreNextMouseMove = true;
    lastMovementTime = glutGet(GLUT_ELAPSED_TIME);
    glutPostRedisplay();
}

// ------------------------------------------------------------
// 화면 출력: 상태 표시창, 실시간 미니맵, 1인칭 3D 장면
// ------------------------------------------------------------
void drawHUD()
{
    char status[128];

    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, 1000, 0, 700);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(1.0f, 1.0f, 1.0f);
    drawText(22.0f, 668.0f,
        "LOST TEMPLE  |  Mouse: Look   WASD: Move   SHIFT: Run   R: Restart   ESC: Quit");

    sprintf_s(status, "KEY: %s", hasKey ? "ACQUIRED" : "NOT FOUND");
    glColor3f(hasKey ? 0.35f : 1.0f, hasKey ? 1.0f : 0.75f, 0.25f);
    drawText(22.0f, 638.0f, status);

    if (gameWon)
    {
        glColor3f(0.25f, 1.0f, 0.35f);
        drawText(390.0f, 350.0f, "YOU ESCAPED THE TEMPLE!");
        glColor3f(1.0f, 1.0f, 1.0f);
        drawText(405.0f, 320.0f, "Press R to play again");
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_LIGHTING);
}

// ------------------------------------------------------------
// 실시간 미니맵 (화면 왼쪽 위)
// ------------------------------------------------------------
void drawMiniMap()
{
    const float tileSize = 8.0f;
    const float mapX = 18.0f;
    const float mapY = (float)windowHeight - 18.0f - ROWS * tileSize;
    const float mapWidth = COLS * tileSize;
    const float mapHeight = ROWS * tileSize;

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, (double)windowWidth, 0.0, (double)windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // 미니맵 뒤에 어두운 배경 패널을 그린다.
    glColor3f(0.04f, 0.045f, 0.05f);
    glBegin(GL_QUADS);
    glVertex2f(mapX - 5.0f, mapY - 5.0f);
    glVertex2f(mapX + mapWidth + 5.0f, mapY - 5.0f);
    glVertex2f(mapX + mapWidth + 5.0f, mapY + mapHeight + 5.0f);
    glVertex2f(mapX - 5.0f, mapY + mapHeight + 5.0f);
    glEnd();

    for (int row = 0; row < ROWS; ++row)
    {
        for (int col = 0; col < COLS; ++col)
        {
            char tile = maze[row][col];
            if (tile == '#') glColor3f(0.48f, 0.47f, 0.43f);
            else if (tile == 'E') glColor3f(0.15f, 0.75f, 0.28f);
            else if (tile == 'K' && !hasKey) glColor3f(1.0f, 0.78f, 0.12f);
            else if (tile == 'V' || tile == 'C') glColor3f(0.65f, 0.42f, 0.25f);
            else glColor3f(0.16f, 0.19f, 0.20f);

            float x0 = mapX + col * tileSize;
            float y0 = mapY + (ROWS - 1 - row) * tileSize;
            glBegin(GL_QUADS);
            glVertex2f(x0, y0);
            glVertex2f(x0 + tileSize - 0.5f, y0);
            glVertex2f(x0 + tileSize - 0.5f, y0 + tileSize - 0.5f);
            glVertex2f(x0, y0 + tileSize - 0.5f);
            glEnd();
        }
    }

    // 플레이어 위치 표시와 현재 바라보는 방향 표시를 그린다.
    float playerMapX = mapX + (playerX / CELL) * tileSize;
    float playerMapY = mapY + (ROWS - playerZ / CELL) * tileSize;
    float dirX = std::sin(playerAngle);
    float dirZ = std::cos(playerAngle);
    float tipX = playerMapX + dirX * 7.0f;
    float tipY = playerMapY - dirZ * 7.0f;
    float sideX = -dirZ * 3.0f;
    float sideY = -dirX * 3.0f;

    glColor3f(0.15f, 0.85f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(tipX, tipY);
    glVertex2f(playerMapX - sideX - dirX * 3.0f,
        playerMapY - sideY + dirZ * 3.0f);
    glVertex2f(playerMapX + sideX - dirX * 3.0f,
        playerMapY + sideY + dirZ * 3.0f);
    glEnd();

    glColor3f(1.0f, 1.0f, 1.0f);
    glRasterPos2f(mapX, mapY - 15.0f);
    const char* label = "MINIMAP  (cyan: you)";
    for (const char* p = label; *p; ++p)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float horizontalScale = std::cos(cameraPitch);
    float dirX = std::sin(playerAngle) * horizontalScale;
    float dirY = std::sin(cameraPitch);
    float dirZ = std::cos(playerAngle) * horizontalScale;

    // 1인칭 카메라: 마우스로 수평 및 수직 시점을 조절한다.
    gluLookAt(playerX, 0.72f, playerZ,
        playerX + dirX, 0.72f + dirY, playerZ + dirZ,
        0.0f, 1.0f, 0.0f);

    // 방향성 조명을 사용해 돌벽의 명암이 잘 드러나도록 한다.
    GLfloat lightPos[] = { -12.0f, 24.0f, 10.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    drawMaze();
    drawHUD();
    drawMiniMap();

    glutSwapBuffers();
}

void reshape(int width, int height)
{
    if (height == 0) height = 1;

    windowWidth = width;
    windowHeight = height;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(68.0, (double)width / (double)height, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
}

// ------------------------------------------------------------
// 입력 처리: 키보드 상태, 연속 이동, 마우스 시점 조절
// ------------------------------------------------------------
void keyboard(unsigned char key, int, int)
{
    switch (key)
    {
    case 'w': case 'W': keyW = true; break;
    case 's': case 'S': keyS = true; break;
    case 'a': case 'A': keyA = true; break;
    case 'd': case 'D': keyD = true; break;
    case 'r': case 'R': restartGame(); break;
    case 27: std::exit(0); break;
    }
}

void keyboardUp(unsigned char key, int, int)
{
    switch (key)
    {
    case 'w': case 'W': keyW = false; break;
    case 's': case 'S': keyS = false; break;
    case 'a': case 'A': keyA = false; break;
    case 'd': case 'D': keyD = false; break;
    }
}

// GLUT가 반복 호출한다. W+A, W+D 등의 키 조합을 동시에 처리할 수 있다.
void updateMovement()
{
    // 프레임마다 고정된 거리만큼 이동하지 않고 실제 경과 시간을 사용한다.
    // 따라서 컴퓨터의 초당 프레임 수가 달라도 이동 속도를 일정하게 유지한다.
    int currentTime = glutGet(GLUT_ELAPSED_TIME);
    if (lastMovementTime == 0)
        lastMovementTime = currentTime;

    float deltaTime = (currentTime - lastMovementTime) / 1000.0f;
    lastMovementTime = currentTime;

    // 창이 일시 정지되거나 이동된 경우 갑자기 크게 이동하는 현상을 방지한다.
    if (deltaTime > 0.05f)
        deltaTime = 0.05f;
    if (deltaTime < 0.0f)
        deltaTime = 0.0f;

    float forwardAmount = 0.0f;
    float strafeAmount = 0.0f;

    // 각 키의 상태를 따로 저장해 W+D, W+A 등의 동시 입력을 허용한다.
    if (keyW) forwardAmount += 1.0f;
    if (keyS) forwardAmount -= 1.0f;
    if (keyD) strafeAmount -= 1.0f;
    if (keyA) strafeAmount += 1.0f;

    if (forwardAmount != 0.0f || strafeAmount != 0.0f)
        movePlayer(forwardAmount, strafeAmount, deltaTime);
}

void mouseLook(int x, int y)
{
    int centerX = windowWidth / 2;
    int centerY = windowHeight / 2;

    if (ignoreNextMouseMove)
    {
        ignoreNextMouseMove = false;
        glutWarpPointer(centerX, centerY);
        return;
    }

    int deltaX = x - centerX;
    int deltaY = y - centerY;

    if (deltaX == 0 && deltaY == 0)
        return;

    playerAngle -= deltaX * mouseSensitivity;
    cameraPitch -= deltaY * mouseSensitivity;

    const float maxPitch = 1.15f;
    if (cameraPitch > maxPitch) cameraPitch = maxPitch;
    if (cameraPitch < -maxPitch) cameraPitch = -maxPitch;

    glutWarpPointer(centerX, centerY);
    glutPostRedisplay();
}

void init()
{
    glClearColor(0.035f, 0.045f, 0.065f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);

    GLfloat ambient[] = { 0.30f, 0.30f, 0.34f, 1.0f };
    GLfloat diffuse[] = { 0.90f, 0.86f, 0.78f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    glShadeModel(GL_SMOOTH);
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1000, 700);
    glutInitWindowPosition(100, 80);
    glutCreateWindow("LOST TEMPLE - 3D Maze");

    glutWarpPointer(windowWidth / 2, windowHeight / 2);
    init();
    lastMovementTime = glutGet(GLUT_ELAPSED_TIME);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutIdleFunc(updateMovement);

    // 마우스 시점 조절: 커서를 숨기고 창의 중앙에 유지한다.
    glutSetCursor(GLUT_CURSOR_NONE);
    glutPassiveMotionFunc(mouseLook);
    glutMotionFunc(mouseLook);

    glutMainLoop();
    return 0;
}
