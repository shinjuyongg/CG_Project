#include <GL/glut.h>
#include <GL/glu.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// TITANIC : LOST SHIP - 2층 수중 난파선 탈출 게임
// Visual Studio + FreeGLUT / OpenGL 고정 기능 파이프라인
//
// 조작:
//   마우스       : 1인칭 시점 회전
//   W / S        : 전진 / 후진
//   A / D        : 좌우 이동
//   SHIFT        : 달리기
//   F            : 손전등 켜기 / 끄기
//   조명          : 1·2층 현창 빛 + 수중 안개 + 바닥 코스틱 + 해초/잔해 조명 + 기포/부유 입자 (자동)
//   SPACE        : 점프 (계단 발판을 한 칸씩 오르기)
//   E            : 현창 조사\n//   SOR OBJ      : 1층 row=9, col=6 위치에 배치 (아래 설정값으로 변경 가능)
//   R            : 게임 재시작
//   ESC          : 종료
//
// 1층: 보물 3개 수집 -> 2층 계단 개방
// 2층: 추격 NPC를 피해 탈출구 도달
// ============================================================

const int FLOOR1_ROWS = 15;
const int FLOOR1_COLS = 15;
const int FLOOR2_ROWS = 15;
const int FLOOR2_COLS = 15;

const char* floor1[FLOOR1_ROWS] = {
    "###############",
    "#S....#.......#",
    "#.##..#..###..#",
    "#....K...#....#",
    "###.####.#.##.#",
    "#...#....#....#",
    "#.#.#.####.##.#",
    "#.#...#....#..#",
    "#.###.#.##.#..#",
    "#...#...#..#..#",
    "#.#.###.#.##..#",
    "#...K...#...U.#",
    "###.#####.##..#",
    "#....K.......##",
    "###############"
};

const char* floor2[FLOOR2_ROWS] = {
    "###############",
    "#S....#.......#",
    "#.....#..###..#",
    "#.....#.......#",
    "###.#####.###.#",
    "#...#.........#",
    "#...#.####.##.#",
    "#...#....#....#",
    "#.#####..#..###",
    "#......#.#....#",
    "#.##...#.#.####",
    "#......#......#",
    "###.####.######",
    "#............E#",
    "###############"
};

const float CELL = 2.0f;
const float WALL_HEIGHT = 2.8f;
const float PLAYER_RADIUS = 0.23f;
const float MOVE_SPEED = 2.1f;
const float RUN_SPEED = 3.8f;
const float PI = 3.14159265358979323846f;
const float FLOOR2_Y = 3.35f;

float playerX = 3.0f, playerZ = 3.0f;
float playerAngle = 0.0f, cameraPitch = 0.0f;
float playerJumpHeight = 0.0f, playerJumpVelocity = 0.0f;
float playerStairHeight = 0.0f;
bool jumpKeyWasDown = false;
float mouseSensitivity = 0.0035f;
int currentFloor = 1;
int windowWidth = 1100, windowHeight = 760;
bool ignoreNextMouseMove = true;
bool flashlightOn = false;
bool portholeInspected = false;
bool gameWon = false, gameLost = false;
bool stairUnlocked = false;
bool messageVisible = true;
char messageText[128] = "Find all 3 treasures in the cargo deck.";

bool keyW = false, keyA = false, keyS = false, keyD = false;
int lastMovementTime = 0;
int treasureCount = 0;
const int TOTAL_TREASURES = 3;

float npcX = 9.0f, npcZ = 11.0f;
float npcSpeed = 0.72f;
float npcWalkTime = 0.0f;
float npcFacingAngle = 0.0f;
float gameStartTime = 0.0f;
float secondFloorStartTime = 0.0f;
const float ESCAPE_TIME_LIMIT = 100.0f;


GLUquadric* quadric = NULL;

// ============================================================
// SOR OBJ 모델 배치 설정
// 좌표는 미로의 월드 좌표가 아니라 row/col 타일 기준으로 지정한다.
// 현재 기본 배치: 1층 row=9, col=6 타일 중앙
// ============================================================
const int SOR_MODEL_FLOOR = 1;       // 배치 층: 1 또는 2
const int SOR_MODEL_ROW = 9;         // 미로 행(row), 위에서부터 0
const int SOR_MODEL_COL = 6;         // 미로 열(col), 왼쪽부터 0
const float SOR_MODEL_SCALE = 0.55f; // 모델 크기
const float SOR_MODEL_ROTATION_Y = 0.0f; // Y축 회전 각도

struct SOR_Triangle {
    float v[3][3];
    float n[3][3];
};
std::vector<SOR_Triangle> sorTriangles;
bool sorModelLoaded = false;

int resolveObjIndex(int index, int count)
{
    return (index > 0) ? index - 1 : count + index;
}

bool loadSORModel(const char* filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::vector<float> vertices;
    std::vector<float> normals;
    std::string line;

    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string type;
        stream >> type;

        if (type == "v") {
            float x, y, z;
            if (stream >> x >> y >> z) {
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
            }
        }
        else if (type == "vn") {
            float x, y, z;
            if (stream >> x >> y >> z) {
                normals.push_back(x); normals.push_back(y); normals.push_back(z);
            }
        }
        else if (type == "f") {
            std::vector<int> vi, ni;
            std::string token;
            while (stream >> token) {
                int vIndex = 0, nIndex = 0, tIndex = 0;
                if (sscanf_s(token.c_str(), "%d//%d", &vIndex, &nIndex) == 2 ||
                    sscanf_s(token.c_str(), "%d/%d/%d", &vIndex, &tIndex, &nIndex) == 3) {
                    vi.push_back(resolveObjIndex(vIndex, (int)vertices.size() / 3));
                    ni.push_back(resolveObjIndex(nIndex, (int)normals.size() / 3));
                }
                else if (sscanf_s(token.c_str(), "%d", &vIndex) == 1) {
                    vi.push_back(resolveObjIndex(vIndex, (int)vertices.size() / 3));
                    ni.push_back(-1);
                }
            }

            // 다각형 면도 삼각형 팬 방식으로 변환
            for (size_t k = 1; k + 1 < vi.size(); ++k) {
                int ids[3] = { vi[0], vi[k], vi[k + 1] };
                int nids[3] = { ni[0], ni[k], ni[k + 1] };
                SOR_Triangle tri;
                for (int a = 0; a < 3; ++a) {
                    int vi3 = ids[a] * 3;
                    for (int c = 0; c < 3; ++c)
                        tri.v[a][c] = vertices[vi3 + c];

                    if (nids[a] >= 0 && nids[a] * 3 + 2 < (int)normals.size()) {
                        int ni3 = nids[a] * 3;
                        for (int c = 0; c < 3; ++c)
                            tri.n[a][c] = normals[ni3 + c];
                    }
                    else {
                        tri.n[a][0] = tri.n[a][1] = tri.n[a][2] = 0.0f;
                    }
                }

                // OBJ에 법선이 없으면 면 법선을 계산
                if (nids[0] < 0 || nids[1] < 0 || nids[2] < 0) {
                    float ax = tri.v[1][0] - tri.v[0][0];
                    float ay = tri.v[1][1] - tri.v[0][1];
                    float az = tri.v[1][2] - tri.v[0][2];
                    float bx = tri.v[2][0] - tri.v[0][0];
                    float by = tri.v[2][1] - tri.v[0][1];
                    float bz = tri.v[2][2] - tri.v[0][2];
                    float nx = ay * bz - az * by;
                    float ny = az * bx - ax * bz;
                    float nz = ax * by - ay * bx;
                    float len = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (len > 0.00001f) { nx /= len; ny /= len; nz /= len; }
                    for (int a = 0; a < 3; ++a) {
                        tri.n[a][0] = nx; tri.n[a][1] = ny; tri.n[a][2] = nz;
                    }
                }
                sorTriangles.push_back(tri);
            }
        }
    }
    sorModelLoaded = !sorTriangles.empty();
    return sorModelLoaded;
}

void getSORModelPosition(float& x, float& z)
{
    x = SOR_MODEL_COL * CELL + CELL * 0.5f;
    z = SOR_MODEL_ROW * CELL + CELL * 0.5f;
}

void drawSORModel(float floorY)
{
    if (!sorModelLoaded || currentFloor != SOR_MODEL_FLOOR) return;

    float x, z;
    getSORModelPosition(x, z);

    glPushMatrix();
    // OBJ 원점은 모델 중심 근처이며 최저 Y=-0.875이므로 바닥에 닿도록 보정
    glTranslatef(x, floorY + 0.875f * SOR_MODEL_SCALE, z);
    glRotatef(SOR_MODEL_ROTATION_Y, 0.0f, 1.0f, 0.0f);
    glScalef(SOR_MODEL_SCALE, SOR_MODEL_SCALE, SOR_MODEL_SCALE);

    GLfloat matA[] = { 0.035f, 0.075f, 0.070f, 1.0f };
    GLfloat matD[] = { 0.16f, 0.31f, 0.27f, 1.0f };
    GLfloat matS[] = { 0.22f, 0.28f, 0.24f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, matA);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, matD);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matS);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);

    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < sorTriangles.size(); ++i) {
        for (int a = 0; a < 3; ++a) {
            glNormal3fv(sorTriangles[i].n[a]);
            glVertex3fv(sorTriangles[i].v[a]);
        }
    }
    glEnd();
    glPopMatrix();
}


// 함수 전방 선언: 아래 파티클 함수에서 먼저 사용됨
float getElapsedSeconds();
float getFloorY();

// ------------------------------------------------------------
// 수중 기포(220개) 및 부유 입자(260개) 시스템
// ------------------------------------------------------------
const int BUBBLE_COUNT = 220;
const int DUST_COUNT = 260;
struct WaterBubble {
    float x, z, y;
    float speed, radius, phase, drift;
    int floor;
};
struct WaterDust {
    float x, y, z;
    float speed, phase, size;
    int floor;
};
WaterBubble waterBubbles[BUBBLE_COUNT];
WaterDust waterDust[DUST_COUNT];

float randomRange(float low, float high)
{
    return low + (high - low) * ((float)rand() / (float)RAND_MAX);
}

void placeInWalkableCell(float& x, float& z, int floorNumber)
{
    const char** maze = (floorNumber == 1) ? floor1 : floor2;
    int rows = (floorNumber == 1) ? FLOOR1_ROWS : FLOOR2_ROWS;
    int cols = (floorNumber == 1) ? FLOOR1_COLS : FLOOR2_COLS;
    for (int attempt = 0; attempt < 100; ++attempt) {
        int r = 1 + rand() % (rows - 2);
        int c = 1 + rand() % (cols - 2);
        if (maze[r][c] != '#') {
            x = c * CELL + CELL * 0.5f + randomRange(-0.48f, 0.48f);
            z = r * CELL + CELL * 0.5f + randomRange(-0.48f, 0.48f);
            return;
        }
    }
    x = CELL * 1.5f;
    z = CELL * 1.5f;
}

void initializeWaterParticles()
{
    srand(20261126);
    for (int i = 0; i < BUBBLE_COUNT; ++i) {
        waterBubbles[i].floor = (rand() % 2) + 1;
        placeInWalkableCell(waterBubbles[i].x, waterBubbles[i].z, waterBubbles[i].floor);
        waterBubbles[i].y = randomRange(0.08f, WALL_HEIGHT + 0.2f);
        waterBubbles[i].speed = randomRange(0.22f, 0.58f);
        waterBubbles[i].radius = randomRange(0.025f, 0.065f);
        waterBubbles[i].phase = randomRange(0.0f, 6.28318f);
        waterBubbles[i].drift = randomRange(0.035f, 0.13f);
    }
    for (int i = 0; i < DUST_COUNT; ++i) {
        waterDust[i].floor = (rand() % 2) + 1;
        placeInWalkableCell(waterDust[i].x, waterDust[i].z, waterDust[i].floor);
        waterDust[i].y = randomRange(0.15f, WALL_HEIGHT + 0.25f);
        waterDust[i].speed = randomRange(0.025f, 0.11f);
        waterDust[i].phase = randomRange(0.0f, 6.28318f);
        waterDust[i].size = randomRange(1.0f, 2.5f);
    }
}

void updateWaterParticles(float dt)
{
    float t = getElapsedSeconds();
    for (int i = 0; i < BUBBLE_COUNT; ++i) {
        WaterBubble& b = waterBubbles[i];
        b.y += b.speed * dt;
        b.x += sinf(t * 1.25f + b.phase) * b.drift * dt;
        b.z += cosf(t * 0.9f + b.phase) * b.drift * dt;
        if (b.y > WALL_HEIGHT + 0.35f) {
            b.y = 0.05f;
            placeInWalkableCell(b.x, b.z, b.floor);
        }
    }
    for (int i = 0; i < DUST_COUNT; ++i) {
        WaterDust& d = waterDust[i];
        d.y += sinf(t * 0.55f + d.phase) * d.speed * dt;
        d.x += cosf(t * 0.7f + d.phase) * d.speed * 0.22f * dt;
        d.z += sinf(t * 0.8f + d.phase) * d.speed * 0.22f * dt;
        if (d.y < 0.12f) d.y = WALL_HEIGHT;
        if (d.y > WALL_HEIGHT + 0.25f) d.y = 0.12f;
    }
}

void drawWaterParticles()
{
    float t = getElapsedSeconds();
    float floorY = getFloorY();
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POINT_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 작은 부유 먼지: 밝기를 낮춰 어두운 심해 분위기를 유지
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < DUST_COUNT; ++i) {
        WaterDust& d = waterDust[i];
        if (d.floor != currentFloor) continue;
        float pulse = 0.45f + 0.35f * (0.5f + 0.5f * sinf(t * 1.7f + d.phase));
        glColor4f(0.48f, 0.66f, 0.67f, pulse * 0.34f);
        glVertex3f(d.x, floorY + d.y, d.z);
    }
    glEnd();

    // 반투명한 작은 구체와 하이라이트로 기포 표현
    for (int i = 0; i < BUBBLE_COUNT; ++i) {
        WaterBubble& b = waterBubbles[i];
        if (b.floor != currentFloor) continue;
        float bobX = b.x + sinf(t * 1.25f + b.phase) * b.drift * 0.35f;
        float bobZ = b.z + cosf(t * 0.9f + b.phase) * b.drift * 0.35f;
        float alpha = 0.20f + 0.10f * (0.5f + 0.5f * sinf(t * 2.0f + b.phase));
        glPushMatrix();
        glTranslatef(bobX, floorY + b.y, bobZ);
        glColor4f(0.48f, 0.76f, 0.79f, alpha);
        glutWireSphere(b.radius, 7, 5);
        glTranslatef(-b.radius * 0.28f, b.radius * 0.28f, b.radius * 0.72f);
        glPointSize(2.0f);
        glBegin(GL_POINTS);
        glColor4f(0.72f, 0.88f, 0.88f, alpha * 0.8f);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glEnd();
        glPopMatrix();
    }
    glDepthMask(GL_TRUE);
    glPopAttrib();
}

// ------------------------------------------------------------
// 공통 유틸리티
// ------------------------------------------------------------
void setMaterial(float r, float g, float b, float shine = 18.0f)
{
    GLfloat color[] = { r, g, b, 1.0f };
    GLfloat spec[] = { 0.25f, 0.25f, 0.25f, 1.0f };
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

int getRows() { return currentFloor == 1 ? FLOOR1_ROWS : FLOOR2_ROWS; }
int getCols() { return currentFloor == 1 ? FLOOR1_COLS : FLOOR2_COLS; }
const char* getMazeRow(int row)
{
    return currentFloor == 1 ? floor1[row] : floor2[row];
}
float getFloorY() { return currentFloor == 1 ? 0.0f : FLOOR2_Y; }

float getElapsedSeconds()
{
    return glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
}

// 선실 조명/현창 빛의 미세한 깜빡임 값.
// 급격하게 꺼졌다 켜지는 대신 여러 주기의 파형을 섞어 오래된 조명처럼 표현한다.
float getLightFlicker(float time, float phase)
{
    float slowWave = 0.5f + 0.5f * sinf(time * 2.1f + phase);
    float fastWave = 0.5f + 0.5f * sinf(time * 7.3f + phase * 1.7f);
    float irregularWave = 0.5f + 0.5f * sinf(time * 13.7f + phase * 2.3f);

    float flicker = 0.78f + 0.12f * slowWave
        + 0.075f * fastWave + 0.025f * irregularWave;

    // 드물게 짧은 전압 저하처럼 밝기가 살짝 떨어지는 구간
    float dip = 0.5f + 0.5f * sinf(time * 1.35f + phase * 3.1f);
    if (dip > 0.965f)
        flicker *= 0.72f;

    if (flicker < 0.58f) flicker = 0.58f;
    if (flicker > 1.02f) flicker = 1.02f;
    return flicker;
}

void setMessage(const char* text)
{
    strncpy_s(messageText, text, sizeof(messageText) - 1);
    messageVisible = true;
    glutPostRedisplay();
}

void findStartPosition(int floorNumber, float& x, float& z)
{
    if (floorNumber == 1) {
        x = 3.0f;
        z = 3.0f;
    }
    else {
        x = 3.0f;
        z = 3.0f;
    }
}

// ------------------------------------------------------------
// SOR 회전 곡면: 난파선 장식용 기둥과 통
// ------------------------------------------------------------
void drawSOR(const float profile[][2], int count,
    float red, float green, float blue)
{
    const int slices = 32;
    setMaterial(red, green, blue, 28.0f);

    for (int i = 0; i < count - 1; ++i) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j) {
            float a = 2.0f * PI * (float)j / (float)slices;
            float c = cosf(a), s = sinf(a);
            float r0 = profile[i][0], y0 = profile[i][1];
            float r1 = profile[i + 1][0], y1 = profile[i + 1][1];
            glNormal3f(c, 0.0f, s);
            glVertex3f(r0 * c, y0, r0 * s);
            glNormal3f(c, 0.0f, s);
            glVertex3f(r1 * c, y1, r1 * s);
        }
        glEnd();
    }
}

void drawBarrel()
{
    const float p[][2] = {
        {0.0f,0.0f},{0.34f,0.0f},{0.42f,0.18f},
        {0.46f,0.55f},{0.42f,0.92f},{0.34f,1.08f},{0.0f,1.08f}
    };
    drawSOR(p, 7, 0.30f, 0.15f, 0.075f);
    for (int i = 0; i < 3; ++i)
        drawCube(0.0f, 0.22f + i * 0.30f, 0.0f, 0.91f, 0.055f, 0.91f,
            0.38f, 0.34f, 0.25f);
}

void drawCrate(float x, float y, float z, float size)
{
    drawCube(x, y + size * 0.5f, z, size, size, size,
        0.28f, 0.17f, 0.09f);
    drawCube(x, y + size * 0.5f, z - size * 0.505f,
        size * 0.12f, size * 0.92f, 0.035f, 0.48f, 0.34f, 0.19f);
    drawCube(x, y + size * 0.5f, z + size * 0.505f,
        size * 0.12f, size * 0.92f, 0.035f, 0.48f, 0.34f, 0.19f);
}

// ------------------------------------------------------------
// 수중 해초: 시간에 따라 줄기 끝이 좌우로 흔들리는 애니메이션
// ------------------------------------------------------------
void drawSeaweed(float x, float floorY, float z, float scale, float phase)
{
    const float time = getElapsedSeconds();
    const int blades = 5;

    glPushMatrix();
    glTranslatef(x, floorY, z);
    glScalef(scale, scale, scale);

    for (int b = 0; b < blades; ++b) {
        float angle = (2.0f * PI * (float)b) / (float)blades;
        float baseX = 0.10f * cosf(angle);
        float baseZ = 0.10f * sinf(angle);
        // 현창 중심 높이(바닥에서 약 1.55)까지 해초가 자라도록 높이를 확장한다.
        // 창문 빛이 해초 줄기와 겹쳐 표면 조명이 눈에 띄게 한다.
        float height = 1.25f + 0.30f * (float)(b % 3);
        float width = 0.095f + 0.022f * (float)(b % 2);
        float sway = 0.16f * sinf(time * 1.15f + phase + b * 0.8f);

        float sideX = -sinf(angle) * width;
        float sideZ = cosf(angle) * width;

        // 줄기 하나를 여러 구간의 삼각형 띠로 구성해 자연스럽게 휘어지게 한다.
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= 8; ++j) {
            float t = (float)j / 8.0f;
            float curve = sway * t * t;
            float centerX = baseX + curve;
            float centerZ = baseZ + 0.045f * sinf(time * 0.8f + phase + t * 3.0f) * t;
            float halfWidth = width * (1.0f - 0.82f * t);
            float green = 0.23f + 0.16f * (1.0f - t) + 0.035f * (float)(b % 2);
            // 지나치게 어두운 녹색을 피하고 조명을 받는 면의 명암이 드러나게 한다.
            glColor3f(0.055f, green, 0.14f);
            glNormal3f(0.0f, 1.0f, 0.0f);
            glVertex3f(centerX - sideX * (halfWidth / width), height * t, centerZ - sideZ * (halfWidth / width));
            glVertex3f(centerX + sideX * (halfWidth / width), height * t, centerZ + sideZ * (halfWidth / width));
        }
        glEnd();
    }
    glPopMatrix();
}

// ------------------------------------------------------------
// 부서진 선박 잔해: 바닥에 흩어진 목재와 휘어진 금속 조각
// ------------------------------------------------------------
void drawWreckagePlank(float x, float y, float z,
    float length, float width, float height, float angle, float red, float green, float blue)
{
    glPushMatrix();
    glTranslatef(x, y + height * 0.5f, z);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glRotatef(7.0f, 0.0f, 0.0f, 1.0f);
    drawCube(0.0f, 0.0f, 0.0f, length, height, width, red, green, blue);
    glPopMatrix();
}

void drawWreckageCluster(float x, float floorY, float z, float scale)
{
    // 서로 다른 방향으로 겹친 파손 목재
    drawWreckagePlank(x, floorY, z, 0.95f * scale, 0.16f * scale,
        0.10f * scale, 22.0f, 0.19f, 0.13f, 0.075f);
    drawWreckagePlank(x + 0.12f * scale, floorY + 0.035f * scale,
        z + 0.12f * scale, 0.72f * scale, 0.13f * scale,
        0.085f * scale, -48.0f, 0.24f, 0.17f, 0.095f);
    drawWreckagePlank(x - 0.10f * scale, floorY + 0.07f * scale,
        z + 0.02f * scale, 0.48f * scale, 0.12f * scale,
        0.075f * scale, 76.0f, 0.14f, 0.115f, 0.085f);

    // 부식된 금속 파편
    glPushMatrix();
    glTranslatef(x + 0.28f * scale, floorY + 0.10f * scale, z - 0.20f * scale);
    glRotatef(-24.0f, 0.0f, 1.0f, 0.0f);
    drawCube(0.0f, 0.0f, 0.0f, 0.42f * scale, 0.055f * scale,
        0.16f * scale, 0.16f, 0.20f, 0.19f);
    glPopMatrix();
}

void drawTreasureChest()
{
    // 상자 본체
    drawCube(0.0f, 0.35f, 0.0f, 0.95f, 0.62f, 0.72f,
        0.30f, 0.16f, 0.055f);
    // 금속 테두리
    drawCube(0.0f, 0.36f, -0.37f, 1.0f, 0.09f, 0.055f,
        0.82f, 0.56f, 0.12f);
    drawCube(0.0f, 0.36f, 0.37f, 1.0f, 0.09f, 0.055f,
        0.82f, 0.56f, 0.12f);
    drawCube(-0.46f, 0.36f, 0.0f, 0.055f, 0.09f, 0.75f,
        0.82f, 0.56f, 0.12f);
    drawCube(0.46f, 0.36f, 0.0f, 0.055f, 0.09f, 0.75f,
        0.82f, 0.56f, 0.12f);
    // 보물의 금빛 효과
    glPushMatrix();
    glTranslatef(0.0f, 0.77f, 0.0f);
    glRotatef(90.0f, 1, 0, 0);
    setMaterial(1.0f, 0.72f, 0.12f, 75.0f);
    glutSolidTorus(0.045, 0.18, 12, 24);
    glPopMatrix();
}

void drawPillar(float x, float y, float z)
{
    const float p[][2] = {
        {0.0f,0.0f},{0.34f,0.0f},{0.34f,0.18f},
        {0.23f,0.32f},{0.23f,2.2f},{0.38f,2.35f},{0.0f,2.35f}
    };
    glPushMatrix();
    glTranslatef(x, y, z);
    drawSOR(p, 7, 0.22f, 0.25f, 0.25f);
    glPopMatrix();
}

// ------------------------------------------------------------
// 바닥과 벽: 오래된 철제 선박 내부 느낌
// ------------------------------------------------------------
void drawFloorTile(float x, float z, int row, int col, float floorY)
{
    float shade = ((row + col) % 2 == 0) ? 0.16f : 0.12f;
    setMaterial(shade * 0.55f, shade * 0.85f, shade, 12.0f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glVertex3f(x, floorY, z);
    glVertex3f(x + CELL, floorY, z);
    glVertex3f(x + CELL, floorY, z + CELL);
    glVertex3f(x, floorY, z + CELL);
    glEnd();

    // 바닥 철판의 얇은 테두리
    setMaterial(0.22f, 0.28f, 0.29f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(x + 0.05f, floorY + 0.012f, z + 0.05f);
    glVertex3f(x + CELL - 0.05f, floorY + 0.012f, z + 0.05f);
    glVertex3f(x + CELL - 0.05f, floorY + 0.012f, z + CELL - 0.05f);
    glVertex3f(x + 0.05f, floorY + 0.012f, z + CELL - 0.05f);
    glEnd();
}

// 창문이 설치될 외벽 셀인지 확인한다.
// side: 0=왼쪽, 1=오른쪽, 2=앞쪽, 3=뒤쪽
bool getPortholeSide(int row, int col, int& side)
{
    // 두 층 모두 같은 크기의 현창을 사용한다. 2층은 작은 미로 크기에 맞춰 간격을 조정한다.
    if (currentFloor == 2) {
        if (col == 0 && (row == 1 || row == 3 || row == 5 || row == 7 || row == 9 || row == 11 || row == 13)) { side = 0; return true; }
        if (col == FLOOR2_COLS - 1 && (row == 1 || row == 3 || row == 5 || row == 7 || row == 9 || row == 11 || row == 13)) { side = 1; return true; }
        if (row == 0 && (col == 3 || col == 7 || col == 11)) { side = 2; return true; }
        if (row == FLOOR2_ROWS - 1 && (col == 3 || col == 7 || col == 11)) { side = 3; return true; }
        return false;
    }

    // 배의 좌우 외벽: 복도 전체에 일정한 간격으로 현창 7개씩 배치
    // 홀수 행을 사용해 벽 사이에 창문이 고르게 분포하도록 한다.
    if (col == 0 && (row == 1 || row == 3 || row == 5 || row == 7 || row == 9 || row == 11 || row == 13)) {
        side = 0; return true;
    }
    if (col == FLOOR1_COLS - 1 && (row == 1 || row == 3 || row == 5 || row == 7 || row == 9 || row == 11 || row == 13)) {
        side = 1; return true;
    }
    // 선수(앞쪽)와 선미(뒤쪽) 외벽에도 현창을 추가한다.
    // 통로 입구/출구 중앙을 피하고 col 3, 7, 11에 배치한다.
    if (row == 0 && (col == 3 || col == 7 || col == 11)) {
        side = 2; return true;
    }
    if (row == FLOOR1_ROWS - 1 && (col == 3 || col == 7 || col == 11)) {
        side = 3; return true;
    }
    return false;
}

void drawWall(float x, float z, float floorY)
{
    // 기본 벽: 낡은 선체의 청회색 철판
    drawCube(x + CELL * 0.5f, floorY + WALL_HEIGHT * 0.5f,
        z + CELL * 0.5f, CELL, WALL_HEIGHT, CELL,
        0.62f, 0.66f, 0.64f);
    drawCube(x + CELL * 0.5f, floorY + WALL_HEIGHT - 0.12f,
        z + CELL * 0.5f, CELL * 0.98f, 0.14f, CELL * 0.98f,
        0.38f, 0.42f, 0.40f);
    drawCube(x + CELL * 0.5f, floorY + 0.16f,
        z + CELL * 0.5f, CELL * 0.99f, 0.18f, CELL * 0.99f,
        0.48f, 0.42f, 0.30f);
}

// 현창이 있는 외벽은 통째로 막지 않고 네 부분으로 나누어 창문 구멍을 만든다.
void drawWallWithPorthole(float x, float z, float floorY, int side)
{
    // 창문이 있는 외벽 셀은 일반 벽과 동일하게 셀 전체를 채우되,
    // 창문이 들어갈 중앙 부분만 비워 실제 구멍처럼 보이게 만든다.
    const float openingWidth = 0.82f;
    const float openingBottom = 1.14f;
    const float openingTop = 1.96f;
    const float openingHeight = openingTop - openingBottom;
    const float sideWidth = (CELL - openingWidth) * 0.5f;
    const float wallCenterX = x + CELL * 0.5f;
    const float wallCenterZ = z + CELL * 0.5f;

    // 창문 위와 아래의 벽은 셀 전체 깊이/너비를 유지한다.
    float lowerHeight = openingBottom;
    drawCube(wallCenterX, floorY + lowerHeight * 0.5f, wallCenterZ,
        side < 2 ? CELL : CELL,
        lowerHeight,
        side < 2 ? CELL : CELL,
        0.62f, 0.66f, 0.64f);

    float upperHeight = WALL_HEIGHT - openingTop;
    drawCube(wallCenterX, floorY + openingTop + upperHeight * 0.5f, wallCenterZ,
        CELL, upperHeight, CELL,
        0.085f, 0.115f, 0.13f);

    // 창문 좌우(또는 앞뒤)의 벽 조각. 구멍은 셀의 중앙에 위치한다.
    if (side < 2) {
        drawCube(wallCenterX, floorY + WALL_HEIGHT * 0.5f,
            z + sideWidth * 0.5f,
            CELL, WALL_HEIGHT, sideWidth,
            0.62f, 0.66f, 0.64f);
        drawCube(wallCenterX, floorY + WALL_HEIGHT * 0.5f,
            z + CELL - sideWidth * 0.5f,
            CELL, WALL_HEIGHT, sideWidth,
            0.62f, 0.66f, 0.64f);
    }
    else {
        drawCube(x + sideWidth * 0.5f, floorY + WALL_HEIGHT * 0.5f,
            wallCenterZ, sideWidth, WALL_HEIGHT, CELL,
            0.62f, 0.66f, 0.64f);
        drawCube(x + CELL - sideWidth * 0.5f, floorY + WALL_HEIGHT * 0.5f,
            wallCenterZ, sideWidth, WALL_HEIGHT, CELL,
            0.62f, 0.66f, 0.64f);
    }

    // 창문 구멍의 위아래에만 얇은 녹슨 보강 테두리를 배치한다.
    if (side < 2) {
        float innerFaceX = (side == 0) ? x + CELL : x;
        drawCube(innerFaceX, floorY + openingBottom,
            wallCenterZ, 0.035f, 0.055f, openingWidth + 0.08f,
            0.24f, 0.17f, 0.10f);
        drawCube(innerFaceX, floorY + openingTop,
            wallCenterZ, 0.035f, 0.055f, openingWidth + 0.08f,
            0.24f, 0.17f, 0.10f);
    }
    else {
        float innerFaceZ = (side == 2) ? z + CELL : z;
        drawCube(wallCenterX, floorY + openingBottom,
            innerFaceZ, openingWidth + 0.08f, 0.055f, 0.035f,
            0.24f, 0.17f, 0.10f);
        drawCube(wallCenterX, floorY + openingTop,
            innerFaceZ, openingWidth + 0.08f, 0.055f, 0.035f,
            0.24f, 0.17f, 0.10f);
    }
}

// 원형 현창: gluDisk는 기본적으로 XY 평면에 있으므로 Y축 회전만 적용한다.
void drawPorthole(float x, float y, float z, float rotateY)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(rotateY, 0.0f, 1.0f, 0.0f);

    // 벽에 밀착된 작은 유리창: 과도하게 밝은 자체 발광색을 줄인다.
    glDisable(GL_LIGHTING);
    glColor3f(0.34f, 0.48f, 0.46f);
    gluDisk(quadric, 0.0, 0.30, 36, 1);
    glEnable(GL_LIGHTING);

    // 얇고 낡은 금속 테두리
    setMaterial(0.25f, 0.22f, 0.15f, 28.0f);
    glutSolidTorus(0.045, 0.325, 10, 36);
    setMaterial(0.10f, 0.16f, 0.19f, 20.0f);
    glutSolidTorus(0.012, 0.275, 8, 32);

    // 유리의 아주 약한 청색 반사선
    glDisable(GL_LIGHTING);
    glColor3f(0.48f, 0.56f, 0.57f);
    glutSolidTorus(0.008, 0.292, 6, 32);
    glEnable(GL_LIGHTING);

    // 작은 고정 볼트
    setMaterial(0.35f, 0.30f, 0.20f, 18.0f);
    for (int i = 0; i < 8; ++i) {
        float a = 2.0f * PI * i / 8.0f;
        glPushMatrix();
        glTranslatef(cosf(a) * 0.325f, sinf(a) * 0.325f, 0.012f);
        glutSolidSphere(0.014, 6, 6);
        glPopMatrix();
    }
    glPopMatrix();
}

// 현창에서 선실 안쪽으로 퍼지는 반투명 푸른빛 기둥
// 현창에서 들어오는 가늘고 선명한 수중 광선.
// 넓은 사각형 조명판 대신 여러 개의 얇은 광선과 작은 부유 입자로 표현한다.
void drawPortholeBeam(float x, float y, float z, float dirX, float dirZ)
{
    const float length = 4.8f;
    const float floorY = getFloorY();
    // 각 현창은 위상이 조금씩 달라 빛이 동시에 깜빡이지 않는다.
    float beamPhase = x * 0.73f + z * 0.19f;
    float beamFlicker = getLightFlicker(getElapsedSeconds(), beamPhase);
    const float sideX = -dirZ;
    const float sideZ = dirX;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
        GL_FOG_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // 깊이 테스트를 유지해 이미 렌더링된 물체 뒤쪽의 빛은 가려지게 한다.
    glDepthMask(GL_FALSE);

    // 아주 희미한 외곽 산란광. 큰 면이 아니라 얇은 광선 주변에만 둔다.
    glBegin(GL_QUADS);
    glColor4f(0.48f, 0.67f, 0.66f, 0.045f * beamFlicker);
    glVertex3f(x, y + 0.02f, z);
    glVertex3f(x + dirX * 0.10f + sideX * 0.10f, y - 0.08f, z + dirZ * 0.10f + sideZ * 0.10f);
    glColor4f(0.36f, 0.53f, 0.53f, 0.0f);
    glVertex3f(x + dirX * length + sideX * 0.34f, floorY + 0.18f, z + dirZ * length + sideZ * 0.34f);
    glVertex3f(x + dirX * length - sideX * 0.34f, floorY + 0.18f, z + dirZ * length - sideZ * 0.34f);
    glEnd();

    // 세 개의 가는 광선: 강한 중심광 + 약한 주변광.
    for (int ray = -1; ray <= 1; ++ray) {
        float offset = ray * 0.12f;
        float nearW = (ray == 0) ? 0.035f : 0.018f;
        float farW = (ray == 0) ? 0.075f : 0.040f;
        float endX = x + dirX * length + sideX * offset;
        float endZ = z + dirZ * length + sideZ * offset;

        glBegin(GL_QUADS);
        glColor4f(0.72f, 0.84f, 0.80f, ((ray == 0) ? 0.22f : 0.105f) * beamFlicker);
        glVertex3f(x + sideX * (offset - nearW), y - 0.02f, z + sideZ * (offset - nearW));
        glVertex3f(x + sideX * (offset + nearW), y - 0.02f, z + sideZ * (offset + nearW));
        glColor4f(0.52f, 0.68f, 0.64f, 0.0f);
        glVertex3f(endX + sideX * farW, floorY + 0.16f, endZ + sideZ * farW);
        glVertex3f(endX - sideX * farW, floorY + 0.16f, endZ - sideZ * farW);
        glEnd();
    }

    // 창문 아래 바닥에 길고 희미한 반사광만 남긴다.
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0.48f, 0.65f, 0.61f, 0.055f * beamFlicker);
    glVertex3f(x + dirX * 2.0f, floorY + 0.018f, z + dirZ * 2.0f);
    for (int i = 0; i <= 36; ++i) {
        float a = 2.0f * PI * (float)i / 36.0f;
        float along = 2.0f + cosf(a) * 1.25f;
        float across = sinf(a) * 0.30f;
        glColor4f(0.40f, 0.56f, 0.53f, 0.0f);
        glVertex3f(x + dirX * along + sideX * across,
            floorY + 0.018f,
            z + dirZ * along + sideZ * across);
    }
    glEnd();

    // 물속에 떠다니는 작은 입자: 광선 안에서만 희미하게 보인다.
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 24; ++i) {
        float t = (float)((i * 37) % 100) / 100.0f;
        float lateral = ((float)((i * 19) % 17) / 16.0f - 0.5f) * 0.42f;
        float px = x + dirX * (0.35f + t * (length - 0.5f)) + sideX * lateral;
        float pz = z + dirZ * (0.35f + t * (length - 0.5f)) + sideZ * lateral;
        float py = y - t * (y - floorY) + 0.04f * sinf((float)i);
        glColor4f(0.72f, 0.82f, 0.76f, (0.10f + 0.08f * (1.0f - t)) * beamFlicker);
        glVertex3f(px, py, pz);
    }
    glEnd();

    glPopAttrib();
}

void drawPortholes(float floorY)
{
    float y = floorY + 1.55f;
    int rows = getRows();
    int cols = getCols();
    float leftX = CELL;
    float rightX = (cols - 1) * CELL;

    if (currentFloor == 1) {
        const int windowRows[7] = { 1, 3, 5, 7, 9, 11, 13 };
        for (int i = 0; i < 7; ++i) {
            float z = windowRows[i] * CELL + CELL * 0.5f;
            drawPorthole(leftX, y, z, 90.0f);
            drawPorthole(rightX, y, z, -90.0f);
            drawPortholeBeam(leftX + 0.02f, y, z, 1.0f, 0.0f);
            drawPortholeBeam(rightX - 0.02f, y, z, -1.0f, 0.0f);
        }
        const int endWindowCols[3] = { 3, 7, 11 };
        float frontZ = CELL;
        float backZ = (rows - 1) * CELL;
        for (int i = 0; i < 3; ++i) {
            float x = endWindowCols[i] * CELL + CELL * 0.5f;
            drawPorthole(x, y, frontZ, 0.0f);
            drawPortholeBeam(x, y, frontZ + 0.02f, 0.0f, 1.0f);
            drawPorthole(x, y, backZ, 180.0f);
            drawPortholeBeam(x, y, backZ - 0.02f, 0.0f, -1.0f);
        }
    }
    else {
        // 2층에도 1층과 동일한 크기의 원형 현창을 배치한다.
        const int windowRows[3] = { 1, 3, 5 };
        for (int i = 0; i < 3; ++i) {
            float z = windowRows[i] * CELL + CELL * 0.5f;
            drawPorthole(leftX, y, z, 90.0f);
            drawPorthole(rightX, y, z, -90.0f);
            drawPortholeBeam(leftX + 0.02f, y, z, 1.0f, 0.0f);
            drawPortholeBeam(rightX - 0.02f, y, z, -1.0f, 0.0f);
        }
        const int endWindowCols[2] = { 2, 4 };
        for (int i = 0; i < 2; ++i) {
            float x = endWindowCols[i] * CELL + CELL * 0.5f;
            drawPorthole(x, y, CELL, 0.0f);
            drawPortholeBeam(x, y, CELL + 0.02f, 0.0f, 1.0f);
            drawPorthole(x, y, (rows - 1) * CELL, 180.0f);
            drawPortholeBeam(x, y, (rows - 1) * CELL - 0.02f, 0.0f, -1.0f);
        }
    }
}

// 바닥에 물결처럼 움직이는 수중 코스틱(caustics) 패턴을 얹는다.
// 실제 굴절 시뮬레이션이 아닌, 애니메이션 격자광으로 물의 일렁임을 표현한다.
void drawWaterCaustics(float floorY)
{
    float t = getElapsedSeconds();
    int rows = getRows(), cols = getCols();
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (getMazeRow(r)[c] == '#') continue;
            float x0 = c * CELL, z0 = r * CELL;
            const int n = 8;
            for (int iz = 0; iz < n; ++iz) {
                float za = z0 + CELL * iz / n;
                float zb = z0 + CELL * (iz + 1) / n;
                glBegin(GL_TRIANGLE_STRIP);
                for (int ix = 0; ix <= n; ++ix) {
                    float x = x0 + CELL * ix / n;
                    for (int k = 0; k < 2; ++k) {
                        float z = (k == 0) ? za : zb;
                        float wave = sinf(x * 2.4f + t * 1.7f) * cosf(z * 2.0f - t * 1.35f)
                            + 0.45f * sinf((x + z) * 3.1f - t * 1.1f);
                        float shimmer = 0.5f + 0.5f * wave / 1.45f;
                        float alpha = 0.018f + shimmer * 0.055f;
                        glColor4f(0.60f, 0.82f, 0.78f, alpha);
                        glVertex3f(x, floorY + 0.028f, z);
                    }
                }
                glEnd();
            }
        }
    }
    glPopAttrib();
}

// ------------------------------------------------------------
// 천장 구조와 위쪽 균열에서 들어오는 수중 광선
// ------------------------------------------------------------
void drawCeilingAndBeams(float floorY)
{
    int rows = getRows(), cols = getCols();
    float width = cols * CELL, depth = rows * CELL;
    float ceilingY = floorY + WALL_HEIGHT + 0.05f;

    // 천장은 어두운 선체 철판과 녹슨 보강대만 남긴다.
    drawCube(width * 0.5f, ceilingY + 0.04f, depth * 0.5f,
        width, 0.12f, depth, 0.045f, 0.060f, 0.070f);
    for (int r = 1; r < rows; r += 3) {
        drawCube(width * 0.5f, ceilingY - 0.05f, r * CELL,
            width, 0.12f, 0.18f, 0.055f, 0.070f, 0.075f);
    }
    for (int c = 2; c < cols; c += 4) {
        drawCube(c * CELL, ceilingY - 0.05f, depth * 0.5f,
            0.16f, 0.12f, depth, 0.055f, 0.070f, 0.075f);
    }
}

// ------------------------------------------------------------
// 계단과 2층 진입 지점
// ------------------------------------------------------------
void drawStaircase(float x, float y, float z, bool unlocked)
{
    float r = unlocked ? 0.24f : 0.20f;
    float g = unlocked ? 0.65f : 0.25f;
    float b = unlocked ? 0.85f : 0.28f;

    // 플레이어가 남쪽(+Z)에서 접근하므로 낮은 발판부터 북쪽(-Z)으로 올라가도록 배치.
    // 각 발판은 폭 1.55, 깊이 0.62이며 높이는 0.20씩 증가한다.
    for (int i = 0; i < 5; ++i) {
        float top = 0.20f * (i + 1);
        float stepZ = z + 0.72f - i * 0.36f;
        drawCube(x, y + top * 0.5f, stepZ,
            1.55f, top, 0.62f, r, g, b);
    }
    // 최상단 랜딩 플랫폼
    drawCube(x, y + 0.60f, z - 1.48f,
        1.70f, 0.20f, 0.76f, 0.20f, 0.28f, 0.30f);
}

void drawCeilingLamp(float x, float y, float z)
{
    drawCube(x, y, z, 0.18f, 0.42f, 0.18f,
        0.20f, 0.15f, 0.08f);
    glPushMatrix();
    glTranslatef(x, y - 0.25f, z);
    setMaterial(1.0f, 0.55f, 0.12f, 80.0f);
    glutSolidSphere(0.16, 16, 16);
    glPopMatrix();
}

// ------------------------------------------------------------
// 맵 오브젝트 배치
// ------------------------------------------------------------
void drawMapObjects(float floorY)
{
    int rows = getRows(), cols = getCols();
    const char* const* unused = NULL;
    (void)unused;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            char tile = getMazeRow(row)[col];
            float x = col * CELL + CELL * 0.5f;
            float z = row * CELL + CELL * 0.5f;

            if (currentFloor == 1) {
                if (tile == 'K' && treasureCount < TOTAL_TREASURES) {
                    // 이미 획득한 보물은 treasureCollected 배열에서 별도 처리
                    // 아래 drawMaze()에서 실제 획득 여부를 확인한다.
                }
                if ((row == 2 && col == 2) || (row == 8 && col == 2)) {
                    glPushMatrix();
                    glTranslatef(x, floorY, z);
                    drawBarrel();
                    glPopMatrix();
                }
                if ((row == 5 && col == 2) || (row == 9 && col == 10)) {
                    drawCrate(x, floorY, z, 0.9f);
                }
                if ((row == 4 && col == 1) || (row == 10 && col == 7)) {
                    drawPillar(x, floorY, z);
                }
            }
            else {
                if ((row == 2 && col == 1) || (row == 4 && col == 4))
                    drawCrate(x, floorY, z, 0.85f);
            }
        }
    }

    // 장식물은 벽과 통로 가장자리에 배치해 미로의 이동 경로를 최대한 비워 둔다.
    if (currentFloor == 1) {
        // 해초를 현창에서 들어오는 실제 광선의 진행 구간에 배치한다.
        // 현창의 z 좌표는 3, 15, 27 부근이며, 해초는 벽에서 안쪽으로
        // 약 0.7 CELL 떨어진 곳에 두어 포인트 라이트의 영향을 받게 한다.
        drawSeaweed(CELL * 1.35f, floorY, CELL * 7.45f, 1.20f, 0.4f);
        drawSeaweed(CELL * 13.35f, floorY, CELL * 7.45f, 1.25f, 1.8f);
        drawSeaweed(CELL * 1.35f, floorY, CELL * 13.45f, 1.15f, 2.7f);
        drawSeaweed(CELL * 13.35f, floorY, CELL * 1.45f, 1.20f, 3.9f);

        drawWreckageCluster(CELL * 1.45f, floorY, CELL * 7.65f, 1.0f);
        drawWreckageCluster(CELL * 13.35f, floorY, CELL * 8.45f, 0.85f);
        drawWreckageCluster(CELL * 3.55f, floorY, CELL * 13.45f, 0.9f);
    }
    else {
        drawSeaweed(CELL * 1.25f, floorY, CELL * 3.2f, 0.8f, 1.1f);
        drawWreckageCluster(CELL * 4.8f, floorY, CELL * 4.9f, 0.65f);
    }

    drawCeilingLamp(CELL * 2.5f, floorY + 2.45f, CELL * 5.0f);
    drawCeilingLamp(CELL * 9.5f, floorY + 2.45f, CELL * 8.0f);
}

// ------------------------------------------------------------
// 보물 상태 및 미로 렌더링
// ------------------------------------------------------------
bool treasureCollected[3] = { false, false, false };
const int treasureRow[3] = { 3, 11, 13 };
const int treasureCol[3] = { 5, 4, 5 };

const int TREASURE_PARTICLE_COUNT = 72;
struct TreasureParticle {
    float x, y, z;
    float vx, vy, vz;
    float life, maxLife;
    float size;
    bool active;
};
TreasureParticle treasureParticles[TREASURE_PARTICLE_COUNT] = {};
float treasureBurstX = 0.0f, treasureBurstY = 0.0f, treasureBurstZ = 0.0f;
float treasureBurstStart = -1000.0f;

void spawnTreasureBurst(float x, float y, float z)
{
    treasureBurstX = x;
    treasureBurstY = y;
    treasureBurstZ = z;
    treasureBurstStart = getElapsedSeconds();

    for (int i = 0; i < TREASURE_PARTICLE_COUNT; ++i) {
        float a = (float)i * 2.39996323f;
        float spread = 0.45f + 0.55f * (float)((i * 17) % 23) / 22.0f;
        TreasureParticle& q = treasureParticles[i];
        q.x = x; q.y = y + 0.72f; q.z = z;
        q.vx = cosf(a) * spread * (0.45f + 0.15f * (i % 3));
        q.vz = sinf(a) * spread * (0.45f + 0.15f * (i % 4));
        q.vy = 0.55f + 0.10f * (i % 6);
        q.maxLife = q.life = 1.15f + 0.65f * (float)(i % 7) / 6.0f;
        q.size = 2.0f + (float)(i % 4);
        q.active = true;
    }
}

void updateTreasureParticles(float dt)
{
    for (int i = 0; i < TREASURE_PARTICLE_COUNT; ++i) {
        TreasureParticle& q = treasureParticles[i];
        if (!q.active) continue;
        q.life -= dt;
        if (q.life <= 0.0f) { q.active = false; continue; }
        q.x += q.vx * dt;
        q.y += q.vy * dt;
        q.z += q.vz * dt;
        q.vy -= 0.42f * dt;
        q.vx *= (1.0f - 0.35f * dt);
        q.vz *= (1.0f - 0.35f * dt);
    }
}

void drawTreasureEffects()
{
    bool any = false;
    for (int i = 0; i < TREASURE_PARTICLE_COUNT; ++i)
        if (treasureParticles[i].active) { any = true; break; }
    if (!any && getElapsedSeconds() - treasureBurstStart > 1.2f) return;

    GLboolean lightingWasOn = glIsEnabled(GL_LIGHTING);
    GLboolean fogWasOn = glIsEnabled(GL_FOG);
    GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    // 획득 직후 보물 위치에 짧게 퍼지는 금빛 후광
    float age = getElapsedSeconds() - treasureBurstStart;
    if (age >= 0.0f && age < 1.0f) {
        float alpha = 0.42f * (1.0f - age);
        float radius = 0.22f + age * 1.15f;
        glPushMatrix();
        glTranslatef(treasureBurstX, treasureBurstY + 0.72f, treasureBurstZ);
        glColor4f(1.0f, 0.68f, 0.16f, alpha);
        glutSolidSphere(radius, 18, 14);
        glPopMatrix();
    }

    glPointSize(4.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < TREASURE_PARTICLE_COUNT; ++i) {
        const TreasureParticle& q = treasureParticles[i];
        if (!q.active) continue;
        float fade = q.life / q.maxLife;
        float pulse = 0.75f + 0.25f * sinf(getElapsedSeconds() * 12.0f + i);
        glColor4f(1.0f, 0.66f + 0.25f * pulse, 0.12f, fade * pulse);
        glVertex3f(q.x, q.y, q.z);
    }
    glEnd();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (depthWasOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (fogWasOn) glEnable(GL_FOG); else glDisable(GL_FOG);
    if (lightingWasOn) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
}

bool isTreasureAt(int row, int col, int& index)
{
    for (int i = 0; i < TOTAL_TREASURES; ++i) {
        if (treasureRow[i] == row && treasureCol[i] == col) {
            index = i;
            return true;
        }
    }
    return false;
}

void drawMaze()
{
    float floorY = getFloorY();
    int rows = getRows(), cols = getCols();

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            float x = col * CELL, z = row * CELL;
            char tile = getMazeRow(row)[col];

            drawFloorTile(x, z, row, col, floorY);
            if (tile == '#') {
                int portholeSide = -1;
                if (getPortholeSide(row, col, portholeSide))
                    drawWallWithPorthole(x, z, floorY, portholeSide);
                else
                    drawWall(x, z, floorY);
            }
            else if (currentFloor == 1) {
                int treasureIndex = -1;
                if (isTreasureAt(row, col, treasureIndex) &&
                    !treasureCollected[treasureIndex]) {
                    glPushMatrix();
                    glTranslatef(x + CELL * 0.5f, floorY, z + CELL * 0.5f);
                    drawTreasureChest();
                    glPopMatrix();
                }
                if (tile == 'U') {
                    glPushMatrix();
                    glTranslatef(x + CELL * 0.5f, floorY, z + CELL * 0.5f);
                    drawStaircase(0.0f, 0.0f, -0.45f, stairUnlocked);
                    glPopMatrix();
                }
            }
            else if (tile == 'E') {
                // 2층 탈출구
                drawCube(x + CELL * 0.5f, floorY + 1.0f, z + 0.15f,
                    1.5f, 2.0f, 0.18f, 0.05f, 0.55f, 0.75f);
                drawCube(x + CELL * 0.5f, floorY + 1.0f, z + 0.02f,
                    1.0f, 1.7f, 0.05f, 0.02f, 0.15f, 0.22f);
            }
        }
    }

    // 현창 광선을 해초/잔해보다 먼저 그린다.
    // 이후 불투명 물체가 깊이 버퍼에 기록되면서 광선을 가려 통과 현상을 방지한다.
    drawPortholes(floorY);
    drawWaterCaustics(floorY);
    drawMapObjects(floorY);
    drawSORModel(floorY); // 사용자가 추가한 SOR OBJ 모델
    drawCeilingAndBeams(floorY);
}

// ------------------------------------------------------------
// 충돌 판정
// ------------------------------------------------------------
// ------------------------------------------------------------
// 장애물 충돌 및 계단 점프
// ------------------------------------------------------------
bool circleHits(float x, float z, float ox, float oz, float radius)
{
    float dx = x - ox, dz = z - oz;
    float limit = PLAYER_RADIUS + radius;
    return dx * dx + dz * dz < limit * limit;
}

void getStairOrigin(float& x, float& z)
{
    // floor1의 U 타일(row 11, col 12) 중앙
    x = 12.0f * CELL + CELL * 0.5f;
    z = 11.0f * CELL + CELL * 0.5f;
}

float getStairGroundHeight(float x, float z)
{
    if (currentFloor != 1) return 0.0f;
    float sx, sz;
    getStairOrigin(sx, sz);

    // 렌더링된 발판과 동일한 중심/폭/깊이를 사용한다.
    if (fabsf(x - sx) > 0.775f + PLAYER_RADIUS) return 0.0f;

    float highest = 0.0f;
    for (int i = 0; i < 5; ++i) {
        float stepZ = sz + 0.72f - i * 0.36f;
        if (fabsf(z - stepZ) <= 0.31f + PLAYER_RADIUS)
            highest = 0.20f * (i + 1);
    }
    // 최상단 플랫폼은 5번째 발판과 자연스럽게 겹치도록 설정
    if (fabsf(z - (sz - 1.48f)) <= 0.38f + PLAYER_RADIUS)
        highest = 1.00f;
    return highest;
}

bool hitsDecorativeObject(float x, float z)
{
    // SOR OBJ 모델과의 원형 근사 충돌
    if (sorModelLoaded && currentFloor == SOR_MODEL_FLOOR) {
        float mx, mz;
        getSORModelPosition(mx, mz);
        if (circleHits(x, z, mx, mz, SOR_MODEL_SCALE * 0.95f))
            return true;
    }

    if (currentFloor == 1) {
        // 아직 획득하지 않은 보물 상자도 장애물로 처리
        for (int i = 0; i < TOTAL_TREASURES; ++i) {
            if (!treasureCollected[i]) {
                float tx = treasureCol[i] * CELL + CELL * 0.5f;
                float tz = treasureRow[i] * CELL + CELL * 0.5f;
                if (circleHits(x, z, tx, tz, 0.34f)) return true;
            }
        }
        // 통
        if (circleHits(x, z, 2.5f * CELL, 2.5f * CELL, 0.42f)) return true;
        if (circleHits(x, z, 2.5f * CELL, 8.5f * CELL, 0.42f)) return true;
        // 나무 상자
        if (circleHits(x, z, 2.5f * CELL, 5.5f * CELL, 0.48f)) return true;
        if (circleHits(x, z, 10.5f * CELL, 9.5f * CELL, 0.48f)) return true;
        // 기둥
        if (circleHits(x, z, 1.5f * CELL, 4.5f * CELL, 0.38f)) return true;
        if (circleHits(x, z, 7.5f * CELL, 10.5f * CELL, 0.38f)) return true;
        // 해초
        if (circleHits(x, z, 1.35f * CELL, 5.45f * CELL, 0.38f)) return true;
        if (circleHits(x, z, 13.35f * CELL, 3.45f * CELL, 0.42f)) return true;
        if (circleHits(x, z, 1.35f * CELL, 11.45f * CELL, 0.36f)) return true;
        if (circleHits(x, z, 13.35f * CELL, 10.45f * CELL, 0.40f)) return true;
        // 잔해
        if (circleHits(x, z, 1.45f * CELL, 7.65f * CELL, 0.58f)) return true;
        if (circleHits(x, z, 13.35f * CELL, 8.45f * CELL, 0.52f)) return true;
        if (circleHits(x, z, 3.55f * CELL, 13.45f * CELL, 0.54f)) return true;
    }
    else {
        if (circleHits(x, z, 1.5f * CELL, 2.5f * CELL, 0.38f)) return true;
        if (circleHits(x, z, 4.8f, 4.9f, 0.42f)) return true;
    }
    return false;
}

bool canPlayerOccupy(float x, float z)
{
    float floorY = getFloorY();
    (void)floorY;
    int rows = getRows(), cols = getCols();

    if (x - PLAYER_RADIUS < 0.0f || z - PLAYER_RADIUS < 0.0f ||
        x + PLAYER_RADIUS >= cols * CELL ||
        z + PLAYER_RADIUS >= rows * CELL)
        return false;

    // 통, 상자, 기둥, 해초, 잔해와의 원형 근사 충돌
    if (hitsDecorativeObject(x, z)) return false;

    // 계단은 한 칸씩만 올라갈 수 있다. 다음 발판의 높이에 점프 중
    // 도달하기 전에는 플레이어의 수평 이동을 막아 발판을 통과하지 못하게 한다.
    if (currentFloor == 1) {
        float requiredHeight = getStairGroundHeight(x, z);
        if (requiredHeight > playerStairHeight + 0.001f) {
            if (!stairUnlocked) return false;
            // 현재 착지 높이에서 바로 다음 20cm 발판만 허용한다.
            if (requiredHeight > playerStairHeight + 0.205f) return false;
            // 점프 중 발이 다음 발판 상단보다 충분히 높아졌을 때만 수평 진입 허용
            if (playerJumpHeight + playerStairHeight < requiredHeight + 0.015f)
                return false;
        }
    }

    int minCol = (int)floor((x - PLAYER_RADIUS) / CELL);
    int maxCol = (int)floor((x + PLAYER_RADIUS) / CELL);
    int minRow = (int)floor((z - PLAYER_RADIUS) / CELL);
    int maxRow = (int)floor((z + PLAYER_RADIUS) / CELL);

    for (int row = minRow; row <= maxRow; ++row) {
        for (int col = minCol; col <= maxCol; ++col) {
            if (row < 0 || row >= rows || col < 0 || col >= cols)
                return false;
            if (getMazeRow(row)[col] != '#')
                continue;

            float left = col * CELL, right = left + CELL;
            float top = row * CELL, bottom = top + CELL;
            float closestX = (x < left) ? left : ((x > right) ? right : x);
            float closestZ = (z < top) ? top : ((z > bottom) ? bottom : z);
            float dx = x - closestX, dz = z - closestZ;
            if (dx * dx + dz * dz < PLAYER_RADIUS * PLAYER_RADIUS)
                return false;
        }
    }
    return true;
}

float distance2D(float x1, float z1, float x2, float z2)
{
    float dx = x1 - x2, dz = z1 - z2;
    return sqrtf(dx * dx + dz * dz);
}

void collectTreasureIfNear()
{
    if (currentFloor != 1) return;

    for (int i = 0; i < TOTAL_TREASURES; ++i) {
        if (treasureCollected[i]) continue;
        float tx = treasureCol[i] * CELL + CELL * 0.5f;
        float tz = treasureRow[i] * CELL + CELL * 0.5f;
        if (distance2D(playerX, playerZ, tx, tz) < 1.05f) {
            treasureCollected[i] = true;
            ++treasureCount;
            spawnTreasureBurst(tx, getFloorY(), tz);
            char msg[128];
            sprintf_s(msg, "TREASURE FOUND!  %d / %d", treasureCount, TOTAL_TREASURES);
            setMessage(msg);
            if (treasureCount >= TOTAL_TREASURES) {
                stairUnlocked = true;
                setMessage("All treasures found! Go to the staircase and press SPACE to jump up.");
            }
        }
    }
}

void enterSecondFloor()
{
    if (!stairUnlocked) {
        setMessage("The staircase is locked. Find all 3 treasures first.");
        return;
    }
    currentFloor = 2;
    playerX = 3.0f;
    playerZ = 3.0f;
    playerJumpHeight = 0.0f;
    playerJumpVelocity = 0.0f;
    playerStairHeight = 0.0f;
    playerAngle = 0.0f;
    cameraPitch = 0.0f;
    npcX = 13.0f;
    npcZ = 11.0f;
    npcWalkTime = 0.0f;
    npcFacingAngle = 0.0f;
    secondFloorStartTime = getElapsedSeconds();
    setMessage("Escape! Something is following you...");
}

void interact()
{
    if (gameWon || gameLost) return;

    if (currentFloor == 1) {
        // 현창 근처에서는 조사 메시지를 표시
        float leftX = CELL - 0.18f;
        float rightX = (FLOOR1_COLS - 1) * CELL + 0.18f;
        const int windowRows[7] = { 1, 3, 5, 7, 9, 11, 13 };
        float nearestWindow = 9999.0f;
        for (int i = 0; i < 7; ++i) {
            float wz = windowRows[i] * CELL + CELL * 0.5f;
            nearestWindow = fminf(nearestWindow, distance2D(playerX, playerZ, leftX, wz));
            nearestWindow = fminf(nearestWindow, distance2D(playerX, playerZ, rightX, wz));
        }
        if (nearestWindow < 2.0f) {
            portholeInspected = true;
            setMessage("PORTHOLE ACTIVATED: Blue light floods the corridor.");
        }
        else {
            int row = (int)(playerZ / CELL);
            int col = (int)(playerX / CELL);
            if (row >= 0 && row < FLOOR1_ROWS && col >= 0 && col < FLOOR1_COLS &&
                floor1[row][col] == 'U') {
                if (!stairUnlocked)
                    setMessage("The staircase is locked. Find all 3 treasures first.");
                else
                    setMessage("Hold a direction and tap SPACE to jump onto each stair.");
            }
            else
                setMessage("Explore the cargo deck and collect all treasures.");
        }
    }
    else {
        setMessage("Keep moving! Reach the exit before the pursuer catches you.");
    }
}

// ------------------------------------------------------------
// 2층 추격 NPC
// ------------------------------------------------------------
void drawNPC(float x, float y, float z)
{
    // 사람처럼 두 발로 걸어오는 심해 괴물. 이동 속도에 맞춰 팔다리를 흔든다.
    float swing = sinf(npcWalkTime * 8.0f) * 27.0f;
    float armSwing = -swing * 0.85f;

    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(npcFacingAngle, 0.0f, 1.0f, 0.0f);

    // 몸통: 낡고 찢어진 잠수복
    setMaterial(0.055f, 0.075f, 0.075f, 10.0f);
    glPushMatrix();
    glTranslatef(0.0f, 1.05f, 0.0f);
    glScalef(0.58f, 0.82f, 0.34f);
    glutSolidSphere(0.62, 18, 16);
    glPopMatrix();

    // 목과 기형적으로 기울어진 머리
    setMaterial(0.075f, 0.095f, 0.09f, 22.0f);
    glPushMatrix();
    glTranslatef(0.0f, 1.72f, -0.015f);
    glRotatef(-9.0f, 0.0f, 0.0f, 1.0f);
    glutSolidSphere(0.29, 18, 16);
    glPopMatrix();

    // 얼굴의 붉은 눈과 입 틈
    glDisable(GL_LIGHTING);
    glColor3f(0.95f, 0.025f, 0.012f);
    glPointSize(5.5f);
    glBegin(GL_POINTS);
    glVertex3f(-0.105f, 1.75f, -0.275f);
    glVertex3f(0.105f, 1.75f, -0.275f);
    glEnd();
    glColor3f(0.025f, 0.008f, 0.006f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(-0.075f, 1.58f, -0.27f);
    glVertex3f(0.075f, 1.57f, -0.27f);
    glEnd();
    glEnable(GL_LIGHTING);

    // 어깨에서 회전하는 긴 팔
    setMaterial(0.045f, 0.065f, 0.065f, 8.0f);
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix();
        glTranslatef(side * 0.34f, 1.38f, 0.0f);
        glRotatef(side * armSwing, 1.0f, 0.0f, 0.0f);
        glTranslatef(0.0f, -0.39f, 0.0f);
        glPushMatrix();
        glScalef(0.16f, 0.48f, 0.17f);
        glutSolidCube(1.0f);
        glPopMatrix();
        glTranslatef(0.0f, -0.30f, -0.015f);
        glutSolidSphere(0.12, 12, 10);
        glPopMatrix();
    }

    // 다리는 반대 방향으로 번갈아 움직인다.
    setMaterial(0.035f, 0.052f, 0.052f, 6.0f);
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix();
        glTranslatef(side * 0.17f, 0.70f, 0.0f);
        glRotatef(side * swing, 1.0f, 0.0f, 0.0f);
        glTranslatef(0.0f, -0.32f, 0.0f);
        glPushMatrix();
        glScalef(0.19f, 0.62f, 0.22f);
        glutSolidCube(1.0f);
        glPopMatrix();
        glTranslatef(0.0f, -0.34f, -0.07f);
        glPushMatrix();
        glScalef(0.23f, 0.13f, 0.38f);
        glutSolidCube(1.0f);
        glPopMatrix();
        glPopMatrix();
    }

    // 어깨와 등에서 튀어나온 뼈/난파선 파편 실루엣
    setMaterial(0.12f, 0.13f, 0.11f, 4.0f);
    glPushMatrix();
    glTranslatef(0.0f, 1.32f, 0.25f);
    glRotatef(18.0f, 1.0f, 0.0f, 0.0f);
    glScalef(0.42f, 0.12f, 0.12f);
    glutSolidCube(1.0f);
    glPopMatrix();

    glPopMatrix();
}

bool findNPCWaypoint(float& targetX, float& targetZ)
{
    // 2층 미로에서 BFS로 플레이어까지 이어지는 다음 칸을 찾는다.
    const int N = FLOOR2_ROWS;
    bool visited[N][N] = {};
    int prevR[N][N], prevC[N][N];
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            prevR[r][c] = prevC[r][c] = -1;

    int sr = (int)(npcZ / CELL), sc = (int)(npcX / CELL);
    int tr = (int)(playerZ / CELL), tc = (int)(playerX / CELL);
    if (sr < 0 || sr >= N || sc < 0 || sc >= N ||
        tr < 0 || tr >= N || tc < 0 || tc >= N)
        return false;

    int qr[N * N], qc[N * N], head = 0, tail = 0;
    qr[tail] = sr; qc[tail++] = sc;
    visited[sr][sc] = true;
    const int dr[4] = { -1, 1, 0, 0 };
    const int dc[4] = { 0, 0, -1, 1 };

    while (head < tail) {
        int r = qr[head], c = qc[head++];
        if (r == tr && c == tc) break;
        for (int k = 0; k < 4; ++k) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nr >= N || nc < 0 || nc >= N ||
                visited[nr][nc] || floor2[nr][nc] == '#')
                continue;
            visited[nr][nc] = true;
            prevR[nr][nc] = r;
            prevC[nr][nc] = c;
            qr[tail] = nr; qc[tail++] = nc;
        }
    }

    if (!visited[tr][tc]) return false;
    int r = tr, c = tc;
    while (!(prevR[r][c] == sr && prevC[r][c] == sc)) {
        if (r == sr && c == sc) return false;
        int pr = prevR[r][c], pc = prevC[r][c];
        if (pr < 0 || pc < 0) return false;
        r = pr; c = pc;
    }
    targetX = c * CELL + CELL * 0.5f;
    targetZ = r * CELL + CELL * 0.5f;
    return true;
}

void updateNPC(float dt)
{
    if (currentFloor != 2 || gameWon || gameLost) return;

    float dx = playerX - npcX;
    float dz = playerZ - npcZ;
    float d = sqrtf(dx * dx + dz * dz);
    if (d < 0.78f) {
        gameLost = true;
        setMessage("CAUGHT! Press R to try again.");
        return;
    }

    // 벽에 막히지 않도록 BFS가 찾은 복도 경로의 다음 칸을 추적한다.
    float targetX = playerX, targetZ = playerZ;
    if (findNPCWaypoint(targetX, targetZ)) {
        dx = targetX - npcX;
        dz = targetZ - npcZ;
        d = sqrtf(dx * dx + dz * dz);
        if (d > 0.05f) {
            float step = npcSpeed * dt;
            if (step > d) step = d;
            npcX += (dx / d) * step;
            npcZ += (dz / d) * step;
            npcFacingAngle = atan2f(dx, dz) * 180.0f / PI + 180.0f;
            if (step > 0.0001f) npcWalkTime += dt;
            else npcWalkTime *= 0.98f;
        }
    }

    if (getElapsedSeconds() - secondFloorStartTime > ESCAPE_TIME_LIMIT) {
        gameLost = true;
        setMessage("TIME OVER! Press R to try again.");
    }
}

void checkExit()
{
    if (currentFloor != 2) return;
    int row = (int)(playerZ / CELL);
    int col = (int)(playerX / CELL);
    if (row >= 0 && row < FLOOR2_ROWS && col >= 0 && col < FLOOR2_COLS &&
        floor2[row][col] == 'E') {
        gameWon = true;
        setMessage("YOU ESCAPED THE SUNKEN SHIP!");
    }
}

// ------------------------------------------------------------
// 플레이어 이동
// ------------------------------------------------------------
void movePlayer(float forwardAmount, float strafeAmount, float dt)
{
    if (gameWon || gameLost) return;

    float forwardX = sinf(playerAngle);
    float forwardZ = cosf(playerAngle);
    float rightX = cosf(playerAngle);
    float rightZ = -sinf(playerAngle);

    float len = sqrtf(forwardAmount * forwardAmount +
        strafeAmount * strafeAmount);
    if (len > 1.0f) {
        forwardAmount /= len;
        strafeAmount /= len;
    }

    float speed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? RUN_SPEED : MOVE_SPEED;
    float dx = (forwardX * forwardAmount + rightX * strafeAmount) * speed * dt;
    float dz = (forwardZ * forwardAmount + rightZ * strafeAmount) * speed * dt;

    float maxStep = PLAYER_RADIUS * 0.35f;
    int steps = (int)ceilf(sqrtf(dx * dx + dz * dz) / maxStep);
    if (steps < 1) steps = 1;
    float sx = dx / steps, sz = dz / steps;

    for (int i = 0; i < steps; ++i) {
        float candidateX = playerX + sx;
        if (canPlayerOccupy(candidateX, playerZ)) playerX = candidateX;
        float candidateZ = playerZ + sz;
        if (canPlayerOccupy(playerX, candidateZ)) playerZ = candidateZ;
    }

    collectTreasureIfNear();
    checkExit();
    glutPostRedisplay();
}

// ------------------------------------------------------------
// HUD 및 미니맵
// ------------------------------------------------------------
void drawHUD()
{
    char line[160];
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(0.72f, 0.93f, 1.0f);
    drawText(22.0f, (float)(windowHeight - 30),
        "TITANIC : LOST SHIP  |  Mouse: Look  WASD: Move  SHIFT: Run  F: Flashlight  SPACE: Jump  E: Interact  P: Next Stage (Admin)  R: Restart");

    if (currentFloor == 1)
        sprintf_s(line, "DECK 1 - TREASURE HUNT     TREASURE: %d / %d     FLASHLIGHT: %s",
            treasureCount, TOTAL_TREASURES, flashlightOn ? "ON" : "OFF");
    else {
        float remain = ESCAPE_TIME_LIMIT - (getElapsedSeconds() - secondFloorStartTime);
        if (remain < 0) remain = 0;
        sprintf_s(line, "DECK 2 - ESCAPE     TIME LEFT: %d sec     FLASHLIGHT: %s",
            (int)remain, flashlightOn ? "ON" : "OFF");
    }
    glColor3f(0.25f, 0.85f, 1.0f);
    drawText(22.0f, (float)(windowHeight - 58), line);

    if (messageVisible) {
        glColor3f(1.0f, 0.78f, 0.32f);
        drawText(22.0f, 28.0f, messageText);
    }

    if (gameWon || gameLost) {
        glColor3f(gameWon ? 0.25f : 1.0f, gameWon ? 1.0f : 0.15f, 0.25f);
        drawText(windowWidth * 0.34f, windowHeight * 0.52f,
            gameWon ? "YOU ESCAPED THE SUNKEN SHIP!" : "YOU WERE CAUGHT!");
        glColor3f(1, 1, 1);
        drawText(windowWidth * 0.40f, windowHeight * 0.47f,
            "Press R to restart");
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void drawMiniMap()
{
    const float tile = currentFloor == 1 ? 9.0f : 15.0f;
    int rows = getRows(), cols = getCols();
    float mapX = 18.0f;
    float mapY = windowHeight - 22.0f - rows * tile;

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(0.015f, 0.045f, 0.065f);
    glBegin(GL_QUADS);
    glVertex2f(mapX - 6, mapY - 6);
    glVertex2f(mapX + cols * tile + 6, mapY - 6);
    glVertex2f(mapX + cols * tile + 6, mapY + rows * tile + 6);
    glVertex2f(mapX - 6, mapY + rows * tile + 6);
    glEnd();

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            char t = getMazeRow(r)[c];
            if (t == '#') glColor3f(0.35f, 0.43f, 0.45f);
            else if (t == 'U') glColor3f(0.15f, 0.85f, 1.0f);
            else if (t == 'E') glColor3f(0.15f, 1.0f, 0.35f);
            else if (t == 'K') glColor3f(1.0f, 0.72f, 0.08f);
            else glColor3f(0.06f, 0.15f, 0.19f);

            float x = mapX + c * tile;
            float y = mapY + (rows - 1 - r) * tile;
            glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + tile - 0.7f, y);
            glVertex2f(x + tile - 0.7f, y + tile - 0.7f);
            glVertex2f(x, y + tile - 0.7f);
            glEnd();
        }
    }

    // 플레이어 표시
    float px = mapX + (playerX / CELL) * tile;
    float py = mapY + (rows - playerZ / CELL) * tile;
    glColor3f(0.1f, 0.95f, 1.0f);
    glPointSize(8);
    glBegin(GL_POINTS);
    glVertex2f(px, py);
    glEnd();

    // 2층에서는 추격자의 위치도 표시
    if (currentFloor == 2) {
        float nx = mapX + (npcX / CELL) * tile;
        float ny = mapY + (rows - npcZ / CELL) * tile;
        glColor3f(1.0f, 0.12f, 0.08f);
        glPointSize(8);
        glBegin(GL_POINTS);
        glVertex2f(nx, ny);
        glEnd();
    }

    glColor3f(0.7f, 0.9f, 1.0f);
    glRasterPos2f(mapX, mapY - 17);
    const char* label = currentFloor == 1 ? "DECK 1 MAP (cyan: you)" : "DECK 2 MAP (red: pursuer)";
    for (const char* p = label; *p; ++p)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ------------------------------------------------------------
// 카메라와 조명
// ------------------------------------------------------------
void setupLighting()
{
    // 심해 분위기: 전체 환경광을 크게 낮춰 창문과 손전등만 눈에 띄게 한다.
    GLfloat ambient[] = { 0.105f, 0.115f, 0.112f, 1.0f };
    GLfloat diffuse[] = { 0.075f, 0.085f, 0.082f, 1.0f };
    GLfloat specular[] = { 0.035f, 0.040f, 0.038f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
    glEnable(GL_LIGHT0);

    // 현창은 총 20개(좌우 14개 + 선수/선미 6개)다.
    // OpenGL 고정 기능 파이프라인의 조명 수 제한을 고려해
    // GL_LIGHT2~GL_LIGHT7의 6개 실제 포인트 라이트를 선실 전체에 분산한다.
    // 나머지 현창도 drawPortholeBeam()으로 동일한 시각적 광선을 낸다.
    {
        float lightX[6], lightZ[6];
        if (currentFloor == 1) {
            lightX[0] = CELL + 0.22f; lightX[1] = CELL + 0.22f;
            lightX[2] = (FLOOR1_COLS - 1) * CELL - 0.22f;
            lightX[3] = (FLOOR1_COLS - 1) * CELL - 0.22f;
            lightX[4] = 7.0f * CELL; lightX[5] = 7.0f * CELL;
            lightZ[0] = 3.0f * CELL; lightZ[1] = 9.0f * CELL;
            lightZ[2] = 3.0f * CELL; lightZ[3] = 9.0f * CELL;
            lightZ[4] = CELL + 0.22f; lightZ[5] = (FLOOR1_ROWS - 1) * CELL - 0.22f;
        }
        else {
            lightX[0] = CELL + 0.22f; lightX[1] = CELL + 0.22f;
            lightX[2] = (FLOOR2_COLS - 1) * CELL - 0.22f;
            lightX[3] = (FLOOR2_COLS - 1) * CELL - 0.22f;
            lightX[4] = 2.5f * CELL; lightX[5] = 4.5f * CELL;
            lightZ[0] = 1.5f * CELL; lightZ[1] = 4.5f * CELL;
            lightZ[2] = 1.5f * CELL; lightZ[3] = 4.5f * CELL;
            lightZ[4] = CELL + 0.22f; lightZ[5] = (FLOOR2_ROWS - 1) * CELL - 0.22f;
        }
        for (int i = 0; i < 6; ++i) {
            float flicker = getLightFlicker(getElapsedSeconds(), 0.83f + i * 1.47f);
            GLfloat lightColor[] = {
                0.20f * flicker, 0.27f * flicker, 0.26f * flicker, 1.0f
            };
            GLfloat softSpecular[] = { 0.030f, 0.038f, 0.036f, 1.0f };
            GLfloat position[] = { lightX[i], getFloorY() + 1.55f, lightZ[i], 1.0f };
            GLenum lightId = GL_LIGHT0 + 2 + i;
            glLightfv(lightId, GL_DIFFUSE, lightColor);
            glLightfv(lightId, GL_SPECULAR, softSpecular);
            glLightfv(lightId, GL_POSITION, position);
            glLightf(lightId, GL_CONSTANT_ATTENUATION, 0.90f);
            glLightf(lightId, GL_LINEAR_ATTENUATION, 0.13f);
            glLightf(lightId, GL_QUADRATIC_ATTENUATION, 0.026f);
            glEnable(lightId);
        }
    }

    if (flashlightOn) {
        GLfloat white[] = { 0.95f, 0.98f, 1.0f, 1.0f };
        GLfloat pos[] = { playerX, getFloorY() + 0.72f, playerZ, 1.0f };
        GLfloat dir[] = { sinf(playerAngle) * cosf(cameraPitch),
            sinf(cameraPitch), cosf(playerAngle) * cosf(cameraPitch) };
        glLightfv(GL_LIGHT1, GL_DIFFUSE, white);
        glLightfv(GL_LIGHT1, GL_SPECULAR, white);
        glLightfv(GL_LIGHT1, GL_POSITION, pos);
        glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, dir);
        glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, 28.0f);
        glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 18.0f);
        glLightf(GL_LIGHT1, GL_CONSTANT_ATTENUATION, 0.7f);
        glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.035f);
        glEnable(GL_LIGHT1);
    }
    else {
        glDisable(GL_LIGHT1);
    }
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float horizontal = cosf(cameraPitch);
    float dirX = sinf(playerAngle) * horizontal;
    float dirY = sinf(cameraPitch);
    float dirZ = cosf(playerAngle) * horizontal;
    float eyeY = getFloorY() + 0.78f + playerStairHeight + playerJumpHeight;

    gluLookAt(playerX, eyeY, playerZ,
        playerX + dirX, eyeY + dirY, playerZ + dirZ,
        0, 1, 0);

    // 은은한 청록색 환경광 + 손전등 스포트라이트
    GLfloat lightPos[] = { playerX + 5.0f, getFloorY() + 2.5f,
        playerZ + 5.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    setupLighting();

    drawMaze();
    drawWaterParticles();
    if (currentFloor == 2)
        drawNPC(npcX, getFloorY(), npcZ);

    // 보물을 획득한 순간에만 금빛 후광과 파티클이 나타난다.
    drawTreasureEffects();

    // 안개는 3D 월드에만 적용하고 HUD/미니맵에는 적용하지 않는다.
    glDisable(GL_FOG);
    drawHUD();
    drawMiniMap();
    glEnable(GL_FOG);
    glutSwapBuffers();
}

// ------------------------------------------------------------
// 창 크기 변경
// ------------------------------------------------------------
void reshape(int width, int height)
{
    if (height == 0) height = 1;
    windowWidth = width;
    windowHeight = height;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)width / (double)height, 0.1, 180.0);
    glMatrixMode(GL_MODELVIEW);
}

// ------------------------------------------------------------
// 입력 처리
// ------------------------------------------------------------
void restartGame()
{
    currentFloor = 1;
    playerX = 3.0f;
    playerZ = 3.0f;
    playerAngle = 0.0f;
    cameraPitch = 0.0f;
    playerJumpHeight = 0.0f;
    playerJumpVelocity = 0.0f;
    playerStairHeight = 0.0f;
    jumpKeyWasDown = false;
    treasureCount = 0;
    for (int i = 0; i < TREASURE_PARTICLE_COUNT; ++i)
        treasureParticles[i].active = false;
    treasureBurstStart = -1000.0f;
    treasureCollected[0] = treasureCollected[1] = treasureCollected[2] = false;
    stairUnlocked = false;
    flashlightOn = false;
    portholeInspected = false;
    gameWon = gameLost = false;
    keyW = keyA = keyS = keyD = false;
    npcX = 13.0f;
    npcZ = 11.0f;
    npcWalkTime = 0.0f;
    npcFacingAngle = 0.0f;
    secondFloorStartTime = 0.0f;
    ignoreNextMouseMove = true;
    lastMovementTime = glutGet(GLUT_ELAPSED_TIME);
    setMessage("Find all 3 treasures in the cargo deck.");
}

void keyboard(unsigned char key, int, int)
{
    switch (key) {
    case 'w': case 'W': keyW = true; break;
    case 's': case 'S': keyS = true; break;
    case 'a': case 'A': keyA = true; break;
    case 'd': case 'D': keyD = true; break;
    case 'f': case 'F':
        flashlightOn = !flashlightOn;
        setMessage(flashlightOn ? "Flashlight ON." : "Flashlight OFF.");
        break;
    case 'e': case 'E': interact(); break;
    case 32: // SPACE: 점프
        if (!jumpKeyWasDown && !gameWon && !gameLost && playerJumpHeight <= 0.001f) {
            playerJumpVelocity = 3.55f;
            jumpKeyWasDown = true;
        }
        break;
    case 'p': case 'P':
        // 관리자용 스테이지 이동: 보물 수집 및 계단 잠금 조건을 건너뛰고 2층으로 이동
        if (currentFloor == 1) {
            stairUnlocked = true;
            enterSecondFloor();
        }
        else {
            setMessage("Already on Deck 2.");
        }
        break;
    case 'r': case 'R': restartGame(); break;
    case 27: std::exit(0); break;
    }
    glutPostRedisplay();
}

void keyboardUp(unsigned char key, int, int)
{
    switch (key) {
    case 'w': case 'W': keyW = false; break;
    case 's': case 'S': keyS = false; break;
    case 'a': case 'A': keyA = false; break;
    case 'd': case 'D': keyD = false; break;
    case 32: jumpKeyWasDown = false; break;
    }
}

void updateMovement()
{
    int now = glutGet(GLUT_ELAPSED_TIME);
    if (lastMovementTime == 0) lastMovementTime = now;
    float dt = (now - lastMovementTime) / 1000.0f;
    lastMovementTime = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f) dt = 0.0f;
    updateTreasureParticles(dt);
    updateWaterParticles(dt);

    // 점프 물리: 발판 착지 판정이 가능하도록 지면 높이에서 먼저 강제 clamp하지 않는다.
    float previousJumpHeight = playerJumpHeight;
    if (playerJumpHeight > 0.0f || playerJumpVelocity > 0.0f) {
        const float ascentGravity = 8.0f;
        const float descentGravity = 13.0f;
        float jumpGravity = (playerJumpVelocity > 0.0f) ? ascentGravity : descentGravity;
        playerJumpHeight += playerJumpVelocity * dt;
        playerJumpVelocity -= jumpGravity * dt;
        if (playerJumpHeight < 0.0f) playerJumpHeight = 0.0f;
    }

    float forward = 0.0f, strafe = 0.0f;
    if (keyW) forward += 1.0f;
    if (keyS) forward -= 1.0f;
    if (keyD) strafe -= 1.0f;
    if (keyA) strafe += 1.0f;

    if (forward != 0.0f || strafe != 0.0f)
        movePlayer(forward, strafe, dt);

    if (currentFloor == 1) {
        float stairHeight = getStairGroundHeight(playerX, playerZ);

        // 하강 중 플레이어의 발이 다음 발판 상단을 통과하는 순간 착지시킨다.
        // 각 착지 때 점프 높이를 0으로 초기화하므로 스페이스바를 다시 눌러야 다음 단으로 오른다.
        if (stairHeight > playerStairHeight + 0.001f && playerJumpVelocity <= 0.0f) {
            float neededJump = stairHeight - playerStairHeight;
            // 한 프레임 사이에 착지 높이를 지나쳐도 놓치지 않도록 교차 판정한다.
            if (previousJumpHeight >= neededJump &&
                playerJumpHeight <= neededJump &&
                stairHeight <= playerStairHeight + 0.205f) {
                playerStairHeight = stairHeight;
                playerJumpHeight = 0.0f;
                playerJumpVelocity = 0.0f;
            }
        }

        // 계단 바깥으로 완전히 이동한 뒤에만 지면 높이로 내려온다.
        if (stairHeight <= 0.001f && playerJumpHeight <= 0.001f &&
            playerJumpVelocity <= 0.0f) {
            playerStairHeight = 0.0f;
        }

        // 최상단 플랫폼(1.00)에 착지한 뒤 북쪽(-Z)으로 조금 더 이동하면 2층으로 이동
        if (stairUnlocked && playerStairHeight >= 0.99f &&
            playerJumpHeight <= 0.02f &&
            playerZ < (11.0f * CELL + CELL * 0.5f - 2.05f)) {
            enterSecondFloor();
        }
    }

    updateNPC(dt);
    glutPostRedisplay();
}

void mouseLook(int x, int y)
{
    int cx = windowWidth / 2, cy = windowHeight / 2;
    if (ignoreNextMouseMove) {
        ignoreNextMouseMove = false;
        glutWarpPointer(cx, cy);
        return;
    }
    int dx = x - cx, dy = y - cy;
    if (dx == 0 && dy == 0) return;

    playerAngle -= dx * mouseSensitivity;
    cameraPitch -= dy * mouseSensitivity;
    const float maxPitch = 1.10f;
    if (cameraPitch > maxPitch) cameraPitch = maxPitch;
    if (cameraPitch < -maxPitch) cameraPitch = -maxPitch;

    glutWarpPointer(cx, cy);
    glutPostRedisplay();
}

// ------------------------------------------------------------
// OpenGL 초기화 및 main
// ------------------------------------------------------------
void init()
{
    glClearColor(0.0010f, 0.0013f, 0.0015f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_FOG);
    GLfloat fogColor[] = { 0.055f, 0.070f, 0.068f, 1.0f };
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.0065f);

    initializeWaterParticles();

    // sor_model.obj를 실행 파일의 작업 폴더에서 읽는다.
    sorModelLoaded = loadSORModel("sor_model.obj");
    if (!sorModelLoaded) {
        // Visual Studio 프로젝트 폴더에서 실행하는 경우를 위한 상위 경로 탐색
        sorModelLoaded = loadSORModel("../sor_model.obj");
    }
    if (!sorModelLoaded) {
        printf("[SOR] sor_model.obj 파일을 찾지 못했습니다. 실행 폴더에 복사하세요.\\n");
    }
    else {
        printf("[SOR] OBJ 모델 로드 완료: %d triangles\\n", (int)sorTriangles.size());
    }

    quadric = gluNewQuadric();
    gluQuadricNormals(quadric, GLU_SMOOTH);
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(windowWidth, windowHeight);
    glutInitWindowPosition(80, 50);
    glutCreateWindow("TITANIC : LOST SHIP - Underwater Escape");

    init();
    lastMovementTime = glutGet(GLUT_ELAPSED_TIME);
    glutWarpPointer(windowWidth / 2, windowHeight / 2);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutIdleFunc(updateMovement);
    glutSetCursor(GLUT_CURSOR_NONE);
    glutPassiveMotionFunc(mouseLook);
    glutMotionFunc(mouseLook);

    glutMainLoop();
    return 0;
}
