// LOST TEMPLE 3D - original map and BFS retained from LOST_TEMPLE_2D_Maze.cpp.
// C++11+, desktop compatibility OpenGL + GLU + GLUT (FreeGLUT recommended).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef MAZE_HEADLESS
#ifdef __APPLE__
#include <GLUT/glut.h>
#include <OpenGL/glu.h>
#else
#include <GL/glut.h>
#include <GL/glu.h>
#endif
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

const int ROWS = 15, COLS = 15;
const float CELL = 1.0f, WALL_HEIGHT = 1.8f;
const float EYE_HEIGHT = 0.62f, PLAYER_RADIUS = 0.18f;
const float MOVE_SPEED = 2.2f, TURN_SPEED = 1.8f;
const float PI = 3.14159265358979323846f;
bool hasKey = false, gameWon = false, showHint = false, solutionExists = false;
std::vector<std::pair<int, int>> solutionPath;
float playerX = 1.5f, playerZ = 1.5f, yaw = PI; // initially face south, down the corridor
int playerRow = 1, playerCol = 1;
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

std::vector<std::pair<int, int>> findPath(
    std::pair<int, int> start,
    std::pair<int, int> goal)
{
    std::queue<std::pair<int, int>> q;

    std::vector<std::vector<bool>> visited(
        ROWS, std::vector<bool>(COLS, false));

    std::vector<std::vector<std::pair<int, int>>> parent(
        ROWS,
        std::vector<std::pair<int, int>>(COLS, { -1, -1 }));

    q.push(start);
    visited[start.first][start.second] = true;

    const int dr[4] = { -1, 1, 0, 0 };
    const int dc[4] = { 0, 0, -1, 1 };

    while (!q.empty())
    {
        std::pair<int, int> current = q.front();
        q.pop();

        if (current == goal)
            break;

        for (int i = 0; i < 4; ++i)
        {
            int nr = current.first + dr[i];
            int nc = current.second + dc[i];

            if (nr < 0 || nr >= ROWS ||
                nc < 0 || nc >= COLS)
                continue;

            if (maze[nr][nc] == '#' || visited[nr][nc])
                continue;

            visited[nr][nc] = true;
            parent[nr][nc] = current;

            q.push({ nr, nc });
        }
    }

    if (!visited[goal.first][goal.second])
        return {};

    std::vector<std::pair<int, int>> path;

    std::pair<int, int> current = goal;

    while (!(current == start))
    {
        path.push_back(current);
        current = parent[current.first][current.second];
    }

    path.push_back(start);

    std::reverse(path.begin(), path.end());

    return path;
}

// ------------------------------------------------------------
// Build complete path: Start -> Key -> Exit
// ------------------------------------------------------------

void buildSolutionPath()
{
    std::pair<int, int> start = { -1, -1 };
    std::pair<int, int> key = { -1, -1 };
    std::pair<int, int> exitCell = { -1, -1 };

    for (int r = 0; r < ROWS; ++r)
    {
        for (int c = 0; c < COLS; ++c)
        {
            if (maze[r][c] == 'S')
                start = { r, c };

            if (maze[r][c] == 'K')
                key = { r, c };

            if (maze[r][c] == 'E')
                exitCell = { r, c };
        }
    }

    if (start.first < 0 || key.first < 0 ||
        exitCell.first < 0)
    {
        solutionExists = false;
        return;
    }

    std::vector<std::pair<int, int>> startToKey =
        findPath(start, key);

    std::vector<std::pair<int, int>> keyToExit =
        findPath(key, exitCell);

    if (startToKey.empty() || keyToExit.empty())
    {
        solutionExists = false;
        return;
    }

    solutionPath = startToKey;

    for (size_t i = 1; i < keyToExit.size(); ++i)
    {
        solutionPath.push_back(keyToExit[i]);
    }

    solutionExists = true;
}


// Game rules: only walls, bounds and a locked exit block the player.
bool canMoveTo(int row, int col) {
    return row >= 0 && row < ROWS && col >= 0 && col < COLS &&
        maze[row][col] != '#' && (maze[row][col] != 'E' || hasKey);
}

// Circle vs. nearby solid tile rectangles, including the map boundary.
bool canStandAt(float x, float z) {
    const float radius = PLAYER_RADIUS;
    int minCol = int(std::floor((x-radius)/CELL));
    int maxCol = int(std::floor((x+radius)/CELL));
    int minRow = int(std::floor((z-radius)/CELL));
    int maxRow = int(std::floor((z+radius)/CELL));
    for (int r=minRow; r<=maxRow; ++r) for (int c=minCol; c<=maxCol; ++c) {
        if (canMoveTo(r,c)) continue;
        float nearestX = std::max(c*CELL, std::min(x,(c+1)*CELL));
        float nearestZ = std::max(r*CELL, std::min(z,(r+1)*CELL));
        float dx=x-nearestX, dz=z-nearestZ;
        if (dx*dx+dz*dz < radius*radius) return false;
    }
    return true;
}

void checkTile() {
    playerRow=int(std::floor(playerZ/CELL));
    playerCol=int(std::floor(playerX/CELL));
    if (maze[playerRow][playerCol]=='K') hasKey=true;
    if (maze[playerRow][playerCol]=='E' && hasKey) gameWon=true;
}

// Small substeps prevent tunnelling; separate axes permit wall sliding.
void movePlayer(float dx, float dz) {
    if (gameWon) return;
    int steps=std::max(1,int(std::ceil(std::sqrt(dx*dx+dz*dz)/0.05f)));
    dx/=steps; dz/=steps;
    for (int i=0;i<steps && !gameWon;++i) {
        if (canStandAt(playerX+dx,playerZ)) playerX+=dx;
        checkTile();
        if (gameWon) break;
        if (canStandAt(playerX,playerZ+dz)) playerZ+=dz;
        checkTile();
    }
}

void resetGame() {
    hasKey=false; gameWon=false; yaw=PI;
    for(int r=0;r<ROWS;++r) for(int c=0;c<COLS;++c) if(maze[r][c]=='S') {
        playerRow=r; playerCol=c;
        playerX=(c+0.5f)*CELL; playerZ=(r+0.5f)*CELL;
    }
    // Keep the H toggle on restart, as in the original.
}

void require(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}

void validateMap() {
    int starts=0, keys=0, exits=0;
    for(int r=0;r<ROWS;++r) {
        require(std::string(maze[r]).size()==COLS,"Map row must have 15 cells");
        for(int c=0;c<COLS;++c) {
            starts+=maze[r][c]=='S'; keys+=maze[r][c]=='K'; exits+=maze[r][c]=='E';
        }
    }
    require(starts==1 && keys==1 && exits==1,"Expected exactly one S, K and E");
}

int selfTest() {
    try {
        validateMap(); buildSolutionPath(); resetGame();
        require(solutionExists,"S-K-E route missing");
        require(!canMoveTo(13,13),"Locked exit must block movement");
        require(canMoveTo(9,11) && canMoveTo(13,3),"V/C must remain passable");
        require(!canStandAt(-0.1f,1.5f),"Boundary collision failed");
        require(!canStandAt(1.1f,1.5f),"Player radius not respected");
        movePlayer(3,0);
        require(playerCol==1 && !hasKey,"Tunnelling through wall");
        resetGame();
        for(size_t i=1;i<solutionPath.size();++i) {
            auto cell=solutionPath[i];
            auto previous=solutionPath[i-1];
            require(std::abs(cell.first-previous.first)+std::abs(cell.second-previous.second)==1,
                    "BFS route contains a nonadjacent step");
            movePlayer((cell.second+0.5f)*CELL-playerX,(cell.first+0.5f)*CELL-playerZ);
            require(playerRow==cell.first && playerCol==cell.second,"Route movement failed");
        }
        require(hasKey && gameWon,"Key/exit rule failed");
        float savedX=playerX, savedZ=playerZ;
        movePlayer(0,-1);
        require(playerX==savedX && playerZ==savedZ,"Movement after victory");
        resetGame();
        require(!hasKey && !gameWon && playerRow==1 && playerCol==1,"Restart failed");
        std::cout<<"PASS: map, BFS, collision, V/C, key, exit, victory, restart. Route edges: "
                 <<solutionPath.size()-1<<'\n';
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

#ifndef MAZE_HEADLESS
int windowWidth=1100, windowHeight=760, lastTime=0;
bool keysDown[256]={}, arrowsDown[4]={};
bool showMap=true, dragging=false;
int lastMouseX=0;

void color(float r,float g,float b) { glColor3f(r,g,b); }
void box(float x,float y,float z,float sx,float sy,float sz) {
    glPushMatrix(); glTranslatef(x,y,z); glScalef(sx,sy,sz);
    glutSolidCube(1.0); glPopMatrix();
}
void floorMark(float x,float z,float size) {
    glNormal3f(0,1,0);
    glBegin(GL_QUADS);
    glVertex3f(x-size,0.012f,z-size); glVertex3f(x-size,0.012f,z+size);
    glVertex3f(x+size,0.012f,z+size); glVertex3f(x+size,0.012f,z-size);
    glEnd();
}

void drawCell3D(int r,int c) {
    float x=(c+0.5f)*CELL,z=(r+0.5f)*CELL;
    char tile=maze[r][c];
    color(0.65f,0.60f,0.47f);
    box(x,-0.04f,z,CELL,0.08f,CELL);
    if(tile=='#') {
        float shade=((r+c)%2)*0.035f;
        color(0.32f+shade,0.36f+shade,0.39f+shade);
        box(x,WALL_HEIGHT/2,z,CELL,WALL_HEIGHT,CELL);
    } else if(tile=='S') {
        color(0.2f,0.8f,0.3f); floorMark(x,z,0.30f);
    } else if(tile=='K' && !hasKey) {
        color(1.0f,0.72f,0.10f);
        glPushMatrix(); glTranslatef(x,0.48f,z);
        glRotatef(float(glutGet(GLUT_ELAPSED_TIME))*0.06f,0,1,0);
        glutSolidTorus(0.025,0.10,10,20);
        box(0,-0.16f,0,0.04f,0.23f,0.04f);
        box(0.055f,-0.25f,0,0.12f,0.04f,0.04f);
        glPopMatrix();
    } else if(tile=='V') {
        // Decorative only: offset within the cell so the central walking line stays clear.
        color(0.60f,0.30f,0.16f);
        glPushMatrix(); glTranslatef(x+0.28f,0.22f,z+0.28f);
        glScalef(0.14f,0.22f,0.14f); glutSolidSphere(1,16,12); glPopMatrix();
        box(x+0.28f,0.44f,z+0.28f,0.13f,0.10f,0.13f);
    } else if(tile=='C') {
        color(0.64f,0.66f,0.68f);
        box(x+0.28f,0.58f,z+0.28f,0.20f,1.16f,0.20f);
        box(x+0.28f,0.08f,z+0.28f,0.32f,0.16f,0.32f);
        box(x+0.28f,1.16f,z+0.28f,0.32f,0.12f,0.32f);
    } else if(tile=='E') {
        color(hasKey?0.2f:0.8f,hasKey?0.8f:0.2f,0.2f);
        if(hasKey) floorMark(x,z,0.43f);
        else box(x,0.65f,z,CELL,1.3f,CELL);
    }
}

void drawSolutionHint3D() {
    if(!showHint || !solutionExists) return;
    glDisable(GL_LIGHTING); color(0.05f,0.9f,1.0f);
    for(const auto& cell:solutionPath)
        floorMark((cell.second+0.5f)*CELL,(cell.first+0.5f)*CELL,0.065f);
    glEnable(GL_LIGHTING);
}

void rect(float x,float y,float w,float h) {
    glBegin(GL_QUADS); glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h); glEnd();
}
void text(float x,float y,const std::string& s) {
    color(0.95f,0.95f,0.90f); glRasterPos2f(x,y);
    for(unsigned char ch:s) glutBitmapCharacter(GLUT_BITMAP_8_BY_13,ch);
}

void drawHUD() {
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0,windowWidth,0,windowHeight);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    color(0.06f,0.07f,0.08f); rect(0,windowHeight-85.0f,float(windowWidth),85);
    text(15,windowHeight-22.0f,"LOST TEMPLE - 3D FIRST PERSON");
    text(15,windowHeight-42.0f,"W/S: forward/back  A/D: strafe  Left/Right: turn  Up/Down: move");
    text(15,windowHeight-62.0f,"Left mouse drag: turn  H: BFS hint  M: map  R: restart  Esc: quit");
    color(0.06f,0.07f,0.08f); rect(0,0,float(windowWidth),48);
    text(15,29,hasKey?"KEY: COLLECTED - Exit unlocked":"KEY: NOT COLLECTED - Find K, then E");
    text(15,10,gameWon?"CLEAR! Press R to play again.":
         (!solutionExists?"ERROR: No S-K-E route.":
          (showHint?"Hint ON: original full S -> K -> E route":"H: show the original BFS route")));
    if(showMap && windowWidth>=330 && windowHeight>=340) {
        const float size=10, left=windowWidth-ROWS*size-16.0f, bottom=64;
        color(0.04f,0.04f,0.04f); rect(left-4,bottom-4,COLS*size+8,ROWS*size+8);
        for(int r=0;r<ROWS;++r) for(int c=0;c<COLS;++c) {
            char t=maze[r][c];
            color(0.62f,0.58f,0.46f);
            if(t=='#') color(0.20f,0.24f,0.27f);
            if(t=='K' && !hasKey) color(1,0.75f,0);
            if(t=='S') color(0.2f,0.8f,0.3f);
            if(t=='E') color(hasKey?0.2f:0.8f,hasKey?0.8f:0.2f,0.2f);
            if(t=='V' || t=='C') color(0.6f,0.35f,0.7f);
            rect(left+c*size,bottom+(ROWS-1-r)*size,size-1,size-1);
        }
        if(showHint && solutionExists) {
            color(0.05f,0.9f,1);
            for(const auto& p:solutionPath)
                rect(left+(p.second+0.5f)*size-1,bottom+(ROWS-p.first-0.5f)*size-1,2,2);
        }
        float px=left+playerX/CELL*size,pz=bottom+(ROWS-playerZ/CELL)*size;
        color(0.15f,0.4f,1); rect(px-2,pz-2,4,4);
        color(1,1,1); glBegin(GL_LINES); glVertex2f(px,pz);
        glVertex2f(px+std::sin(yaw)*8,pz+std::cos(yaw)*8); glEnd();
    }
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(playerX,EYE_HEIGHT,playerZ,
              playerX+std::sin(yaw),EYE_HEIGHT,playerZ-std::cos(yaw),0,1,0);
    const GLfloat sun[]={-0.4f,1.0f,0.3f,0.0f};
    glLightfv(GL_LIGHT0,GL_POSITION,sun);
    for(int r=0;r<ROWS;++r) for(int c=0;c<COLS;++c) drawCell3D(r,c);
    drawSolutionHint3D(); drawHUD(); glutSwapBuffers();
}
void reshape(int w,int h) {
    windowWidth=std::max(w,1); windowHeight=std::max(h,1);
    glViewport(0,0,windowWidth,windowHeight);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(65.0,double(windowWidth)/windowHeight,0.05,50.0);
    glMatrixMode(GL_MODELVIEW);
}
unsigned char lowerKey(unsigned char k) {
    return (k>='A' && k<='Z')?static_cast<unsigned char>(k-'A'+'a'):k;
}
void clearInput() {
    std::fill(keysDown,keysDown+256,false);
    std::fill(arrowsDown,arrowsDown+4,false); dragging=false;
}
void keyboard(unsigned char k,int,int) {
    k=lowerKey(k); bool first=!keysDown[k]; keysDown[k]=true;
    if(k==27) std::exit(0);
    if(!first) return;
    if(k=='h') showHint=!showHint;
    if(k=='m') showMap=!showMap;
    if(k=='r') { resetGame(); clearInput(); }
}
void keyboardUp(unsigned char k,int,int) { keysDown[lowerKey(k)]=false; }
int arrowIndex(int key) {
    switch(key) {
        case GLUT_KEY_LEFT:return 0; case GLUT_KEY_RIGHT:return 1;
        case GLUT_KEY_UP:return 2; case GLUT_KEY_DOWN:return 3;
        default:return -1;
    }
}
void specialKeyboard(int k,int,int) { int i=arrowIndex(k); if(i>=0) arrowsDown[i]=true; }
void specialUp(int k,int,int) { int i=arrowIndex(k); if(i>=0) arrowsDown[i]=false; }
void mouse(int button,int state,int x,int) {
    if(button==GLUT_LEFT_BUTTON) { dragging=state==GLUT_DOWN; lastMouseX=x; }
}
void motion(int x,int) {
    if(dragging && !gameWon) yaw+=(x-lastMouseX)*0.006f;
    lastMouseX=x;
}
void entry(int state) { if(state==GLUT_LEFT) clearInput(); }
void visibility(int state) { if(state!=GLUT_VISIBLE) clearInput(); }
void update(int) {
    int now=glutGet(GLUT_ELAPSED_TIME);
    float dt=std::max(0.0f,std::min((now-lastTime)*0.001f,0.05f)); lastTime=now;
    if(!gameWon) {
        yaw+=(int(arrowsDown[1])-int(arrowsDown[0]))*TURN_SPEED*dt;
        yaw=std::fmod(yaw,2*PI);
        float forward=float((keysDown['w']||arrowsDown[2])-(keysDown['s']||arrowsDown[3]));
        float side=float(int(keysDown['d'])-int(keysDown['a']));
        float length=std::sqrt(forward*forward+side*side);
        if(length>0) {
            forward/=length; side/=length;
            movePlayer((std::sin(yaw)*forward+std::cos(yaw)*side)*MOVE_SPEED*dt,
                       (-std::cos(yaw)*forward+std::sin(yaw)*side)*MOVE_SPEED*dt);
        }
    }
    glutPostRedisplay(); glutTimerFunc(16,update,0);
}
#endif

int main(int argc,char** argv) {
#ifdef MAZE_HEADLESS
    (void)argc; (void)argv;
    return selfTest();
#else
    if(argc>1 && std::string(argv[1])=="--self-test") return selfTest();
    try { validateMap(); buildSolutionPath(); resetGame(); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB|GLUT_DEPTH);
    glutInitWindowSize(windowWidth,windowHeight);
    glutCreateWindow("LOST TEMPLE - 3D Maze Challenge");
    glClearColor(0.10f,0.13f,0.17f,1);
    glEnable(GL_DEPTH_TEST); glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    const GLfloat ambient[]={0.45f,0.45f,0.45f,1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
    reshape(windowWidth,windowHeight);
    glutDisplayFunc(display); glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard); glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialKeyboard); glutSpecialUpFunc(specialUp);
    glutMouseFunc(mouse); glutMotionFunc(motion);
    glutEntryFunc(entry); glutVisibilityFunc(visibility);
    glutIgnoreKeyRepeat(1);
    lastTime=glutGet(GLUT_ELAPSED_TIME); glutTimerFunc(16,update,0);
    glutMainLoop(); return 0;
#endif
}

