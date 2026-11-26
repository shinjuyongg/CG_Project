// SOR Modeler | Windows desktop, C++17, OpenGL 1.1
// Visual Studio x64 Native Tools: cl /EHsc /std:c++17 /utf-8 SOR_Modeler.cpp
// 왼쪽에 단면을 그리고 오른쪽에서 회전 결과를 확인한다.
// Shift+클릭: 직전 점과 같은 높이. Ctrl+클릭: 가까운 기존 점 위에서도 새 점 추가.
// K: 마지막 점과 첫 점 연결. C: 곡선/직선 전환. Ctrl+S: 현재 메시를 OBJ로 저장.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <utility>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")

// 기본 기하 자료형. 단면에서 r은 회전축으로부터의 거리이고 h는 축 방향 높이다.
struct Point {
    double r, h;
};

struct Vec {
    double x, y, z;
};

Vec operator-(Vec a, Vec b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec cross(Vec a, Vec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec unit(Vec v) {
    double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return n > 0 ? Vec{v.x / n, v.y / n, v.z / n} : Vec{0, 1, 0};
}

struct Face {
    int a, b, c;
    int flatNormal = -1; // 수평면은 테두리 정점을 공유하면서 별도의 법선을 쓴다.
};

struct Mesh {
    std::vector<Vec> vertices, normals;
    std::vector<Face> faces;
    std::vector<Vec> flatNormals;
    int vertex(Vec v) {
        vertices.push_back(v);
        normals.push_back({});
        return static_cast<int>(vertices.size()) - 1;
    }
    void face(int a, int b, int c, int flatNormal = -1) {
        faces.push_back({a, b, c, flatNormal});
        if (flatNormal >= 0) {
            return;
        }
        Vec normal = cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
        for (int i : {a, b, c}) {
            normals[i].x += normal.x;
            normals[i].y += normal.y;
            normals[i].z += normal.z;
        }
    }
};

const std::vector<Point> vase = {{.34, -.86}, {.41, -.82}, {.30, -.72}, {.28, -.61},
                                 {.43, -.35}, {.55, -.06}, {.51, .17},  {.29, .39},
                                 {.23, .54},  {.27, .70},  {.36, .78},  {.35, .82}};
std::vector<Point> curve;
Mesh mesh;
int divisions = 32;
bool smooth = true, wire = false, light = true;
const double tau = 6.283185307179586;

// 단면 곡선 ---------------------------------------------------------------
// 매개변수는 점 사이의 거리다. r과 h를 독립적으로 보간하므로 같은 높이와
// 하강 구간도 허용된다. 각 좌표가 방향을 바꾸면 그 좌표의 접선을 0으로 둔다.
bool samePoint(Point a, Point b) {
    return a.r == b.r && a.h == b.h;
}

double limitedSlope(double incoming, double outgoing) {
    if (incoming * outgoing <= 0.0) {
        return 0.0;
    }
    return 2.0 * incoming * outgoing / (incoming + outgoing);
}

double hermiteCoordinate(double a, double b, double slopeA, double slopeB, double length,
                         double t) {
    if (a == b) {
        return a; // 평평한 바닥과 수직 벽을 오차 없이 유지한다.
    }
    double u = 1.0 - t;
    return a + (b - a) * t * t * (3.0 - 2.0 * t) + length * t * u * (u * slopeA - t * slopeB);
}

std::vector<Point> sampleProfile(const std::vector<Point>& controls, bool smoothCurve,
                                 bool closeLoop) {
    std::vector<Point> points;
    for (Point point : controls) {
        if (points.empty() || !samePoint(points.back(), point)) {
            points.push_back(point);
        }
    }
    if (points.size() < 3) {
        return points;
    }
    if (samePoint(points.front(), points.back())) {
        if (points.size() == 3) {
            return points; // A-B-A: 두 구간을 그대로 유지한다.
        }
        points.pop_back();
        closeLoop = true;
    }
    if (!smoothCurve) {
        if (closeLoop) {
            points.push_back(points.front());
        }
        return points;
    }

    const size_t count = points.size();
    const size_t segments = closeLoop ? count : count - 1;
    std::vector<double> lengths(segments);
    std::vector<Point> slopes(segments);
    for (size_t i = 0; i < segments; ++i) {
        Point a = points[i], b = points[(i + 1) % count];
        lengths[i] = std::hypot(b.r - a.r, b.h - a.h);
        slopes[i] = {(b.r - a.r) / lengths[i], (b.h - a.h) / lengths[i]};
    }

    std::vector<Point> tangents(count);
    for (size_t i = 0; i < count; ++i) {
        if (!closeLoop && i == 0) {
            tangents[i] = slopes.front();
        } else if (!closeLoop && i == count - 1) {
            tangents[i] = slopes.back();
        } else {
            Point before = slopes[(i + segments - 1) % segments], after = slopes[i];
            tangents[i] = {limitedSlope(before.r, after.r), limitedSlope(before.h, after.h)};
        }
    }

    constexpr int steps = 6;
    std::vector<Point> result;
    for (size_t i = 0; i < segments; ++i) {
        size_t j = (i + 1) % count;
        Point a = points[i], b = points[j];
        result.push_back(a);
        // 수평선과 수직선은 양 끝점만으로 정확히 표현된다.
        if (a.r == b.r || a.h == b.h) {
            continue;
        }
        for (int step = 1; step < steps; ++step) {
            double t = static_cast<double>(step) / steps;
            result.push_back(
                {hermiteCoordinate(a.r, b.r, tangents[i].r, tangents[j].r, lengths[i], t),
                 hermiteCoordinate(a.h, b.h, tangents[i].h, tangents[j].h, lengths[i], t)});
        }
    }
    result.push_back(closeLoop ? points.front() : points.back());
    return result;
}

// 회전 메시 ---------------------------------------------------------------
// 축 위의 점은 정점 하나, 축 밖의 점은 정점 N개의 원이다. 이웃 원만 연결한다.
struct ProfileRing {
    int first, count;
    int at(int step) const {
        return first + step % count;
    }
};

Mesh revolve(const std::vector<Point>& profile, int sides) {
    Mesh result;
    if (profile.size() < 2 || sides < 3) {
        return result;
    }
    const bool closed = samePoint(profile.front(), profile.back());
    const size_t ringCount = profile.size() - (closed ? 1 : 0);
    std::vector<ProfileRing> rings;

    for (size_t i = 0; i < ringCount; ++i) {
        Point p = profile[i];
        int count = p.r == 0.0 ? 1 : sides;
        rings.push_back({static_cast<int>(result.vertices.size()), count});
        for (int j = 0; j < count; ++j) {
            double angle = tau * j / sides;
            result.vertex({p.r * std::cos(angle), p.h, p.r * std::sin(angle)});
        }
    }

    // 축으로 닫은 단면의 부호 있는 면적으로 진행 방향을 판별한다.
    // 같은 컵을 안쪽부터 또는 바깥쪽부터 그려도 벽의 앞면 방향이 같다.
    double signedArea = 0.0;
    for (size_t i = 1; i < profile.size(); ++i) {
        Point a = profile[i - 1], b = profile[i];
        signedArea += (a.r + b.r) * (b.h - a.h);
    }
    bool reverse = signedArea < 0.0;
    if (signedArea == 0.0) {
        reverse = profile.back().r > profile.front().r;
    }
    result.flatNormals = {{0, 1, 0}, {0, -1, 0}};

    for (size_t i = 1; i < profile.size(); ++i) {
        Point a = profile[i - 1], b = profile[i];
        ProfileRing from = rings[i - 1], to = rings[i % ringCount];
        int flatNormal = -1;
        if (a.h == b.h) {
            bool facesUp = (b.r < a.r) != reverse;
            flatNormal = facesUp ? 0 : 1;
        }
        auto addTriangle = [&](int v0, int v1, int v2) {
            if (reverse) {
                std::swap(v1, v2);
            }
            result.face(v0, v1, v2, flatNormal);
        };
        for (int j = 0; j < sides; ++j) {
            if (from.count > 1) {
                addTriangle(from.at(j), to.at(j), from.at(j + 1));
            }
            if (to.count > 1) {
                addTriangle(from.at(j + 1), to.at(j), to.at(j + 1));
            }
        }
    }
    for (Vec& normal : result.normals) {
        normal = unit(normal);
    }
    return result;
}

// 부품과 좌표 변환 ─ 각 부품은 고유한 회전축과 로컬 단면을 갖는다.
struct Part {
    Vec origin{0, 0, 0};
    double angle = 0; // In the XY plane, from +Y towards +X (degrees).
    double tilt = 0;  // Out of the XY plane, towards +Z (degrees).
    bool ring = false;
    std::vector<Point> profile; // 클릭 순서를 유지하는 (반지름, 높이) 목록.
    bool closeProfile = false;
    bool capStart = false;                 // 기존 꽃병 예제는 시작 원판을 사용한다.
    double radius = .32, thickness = .065; // Ring centreline radius and tube radius.
    bool sweep = false;
    std::vector<Point> path; // For a sweep, r and h are signed X and Y coordinates.
    double tubeRadius = .055;
};

struct AxisFrame {
    Vec x, y, z;
    Vec rotate(Vec v) const {
        return {x.x * v.x + y.x * v.y + z.x * v.z, x.y * v.x + y.y * v.y + z.y * v.z,
                x.z * v.x + y.z * v.y + z.z * v.z};
    }
};

inline AxisFrame frameFor(const Part& part) {
    double a = part.angle * tau / 360, t = part.tilt * tau / 360;
    // The original modelling axis is local Y. Local X remains in the XY plane.
    Vec x{std::cos(a), -std::sin(a), 0};
    Vec y{std::sin(a) * std::cos(t), std::cos(a) * std::cos(t), std::sin(t)};
    return {x, y, cross(x, y)};
}

inline Vec axisDirection(const Part& part) {
    return frameFor(part).y;
}

inline Vec translated(Vec p, Vec offset) {
    return {p.x + offset.x, p.y + offset.y, p.z + offset.z};
}

inline Vec toWorld(const Part& part, Vec localPoint) {
    return translated(frameFor(part).rotate(localPoint), part.origin);
}

// 손잡이 메시 ─ 원형 손잡이와 경로를 따라 생성하는 튜브.
// A ring handle is an offset circle revolved through a full turn. Both seams
// wrap to their first sample, so there are no duplicate vertices or end caps.
inline Mesh ringMesh(double radius, double thickness, int n) {
    Mesh result;
    if (thickness <= 0 || radius <= thickness || n < 3) {
        return result;
    }
    const int tubeSteps = 16;
    for (int i = 0; i < tubeSteps; ++i) {
        double u = tau * i / tubeSteps;
        double r = radius + thickness * std::cos(u);
        double h = thickness * std::sin(u);
        for (int j = 0; j < n; ++j) {
            double angle = tau * j / n;
            result.vertex({r * std::cos(angle), h, r * std::sin(angle)});
        }
    }
    for (int i = 0; i < tubeSteps; ++i) {
        int row = i * n, nextRow = ((i + 1) % tubeSteps) * n;
        for (int j = 0; j < n; ++j) {
            int next = (j + 1) % n;
            result.face(row + j, nextRow + j, row + next);
            result.face(row + next, nextRow + j, nextRow + next);
        }
    }
    for (Vec& normal : result.normals) {
        normal = unit(normal);
    }
    return result;
}

// Blend the incoming and outgoing directions at a bend. Using unit directions
// keeps a short segment from overshooting just because its neighbour is long.
inline Point pathDirection(Point a, Point b) {
    double length = std::hypot(b.r - a.r, b.h - a.h);
    return {(b.r - a.r) / length, (b.h - a.h) / length};
}

inline std::vector<Point> sweepPathSamples(const std::vector<Point>& path, bool smoothPath) {
    std::vector<Point> points;
    for (Point p : path) {
        if (points.empty() || std::hypot(p.r - points.back().r, p.h - points.back().h) > 1e-7) {
            points.push_back(p);
        }
    }
    if (!smoothPath || points.size() < 3) {
        return points;
    }

    std::vector<Point> tangent(points.size());
    tangent.front() = pathDirection(points[0], points[1]);
    tangent.back() = pathDirection(points[points.size() - 2], points.back());
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        Point before = pathDirection(points[i - 1], points[i]);
        Point after = pathDirection(points[i], points[i + 1]);
        tangent[i] = {(before.r + after.r) * .5, (before.h + after.h) * .5};
    }
    std::vector<Point> result;
    // Cubic Hermite interpolation passes through every clicked point.
    constexpr int steps = 6;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        Point a = points[i], b = points[i + 1];
        double length = std::hypot(b.r - a.r, b.h - a.h);
        for (int k = 0; k < steps; ++k) {
            double t = double(k) / steps, u = 1 - t;
            double wa = u * u * (1 + 2 * t), wb = t * t * (3 - 2 * t);
            double ma = length * u * u * t, mb = -length * u * t * t;
            result.push_back({wa * a.r + wb * b.r + ma * tangent[i].r + mb * tangent[i + 1].r,
                              wa * a.h + wb * b.h + ma * tangent[i].h + mb * tangent[i + 1].h});
        }
    }
    result.push_back(points.back());
    return result;
}

inline Mesh sweepMesh(const std::vector<Point>& path, double tubeRadius, bool smoothPath, int n) {
    Mesh tube;
    std::vector<Point> centers = sweepPathSamples(path, smoothPath);
    if (centers.size() < 2 || tubeRadius <= 0 || n < 3) {
        return tube;
    }

    for (size_t i = 0; i < centers.size(); ++i) {
        Point center = centers[i];
        Point tangent =
            i == 0 ? pathDirection(center, centers[1]) : pathDirection(centers[i - 1], center);
        if (i > 0 && i + 1 < centers.size()) {
            Point next = pathDirection(center, centers[i + 1]);
            Point middle{tangent.r + next.r, tangent.h + next.h};
            if (std::hypot(middle.r, middle.h) > 1e-7) {
                tangent = pathDirection({0, 0}, middle);
            }
        }
        // The circle uses two axes perpendicular to the path: its in-plane
        // normal (-dy,dx,0), and the fixed Z direction (0,0,1).
        for (int j = 0; j < n; ++j) {
            double angle = tau * j / n, offset = tubeRadius * std::cos(angle);
            tube.vertex({center.r - tangent.h * offset, center.h + tangent.r * offset,
                         tubeRadius * std::sin(angle)});
        }
    }
    for (int i = 0; i + 1 < (int)centers.size(); ++i) {
        for (int j = 0; j < n; ++j) {
            int a = i * n + j, b = i * n + (j + 1) % n;
            tube.face(a, b, a + n);
            tube.face(b, b + n, a + n);
        }
    }
    int firstCap = tube.vertex({centers.front().r, centers.front().h, 0});
    int lastCap = tube.vertex({centers.back().r, centers.back().h, 0});
    int lastRing = (static_cast<int>(centers.size()) - 1) * n;
    for (int j = 0; j < n; ++j) {
        int next = (j + 1) % n;
        tube.face(firstCap, next, j);
        tube.face(lastCap, lastRing + j, lastRing + next);
    }
    for (Vec& normal : tube.normals) {
        normal = unit(normal);
    }
    return tube;
}

inline Mesh partMesh(const Part& part, bool smoothProfile, int n) {
    if (n < 3) {
        return {};
    }
    Mesh result;
    if (part.sweep) {
        result = sweepMesh(part.path, part.tubeRadius, smoothProfile, n);
    } else if (part.ring) {
        result = ringMesh(part.radius, part.thickness, n);
    } else {
        auto profile = sampleProfile(part.profile, smoothProfile, part.closeProfile);
        // 자유 단면에는 그린 면만 생긴다. 시작 원판은 명시적으로 켠 경우에만 추가한다.
        if (part.capStart && !part.closeProfile && profile.size() >= 2 &&
            !samePoint(profile.front(), profile.back()) && profile.front().r > 0.0 &&
            !(profile[1].h == profile.front().h && profile[1].r < profile.front().r)) {
            profile.insert(profile.begin(), Point{0.0, profile.front().h});
        }
        result = revolve(profile, n);
    }
    AxisFrame frame = frameFor(part);
    for (Vec& point : result.vertices) {
        point = translated(frame.rotate(point), part.origin);
    }
    // Normals are directions. Only the rotation applies to them.
    for (Vec& normal : result.normals) {
        normal = frame.rotate(normal);
    }
    for (Vec& normal : result.flatNormals) {
        normal = frame.rotate(normal);
    }
    return result;
}

inline Mesh sceneMesh(const std::vector<Part>& parts, bool smoothProfile, int n) {
    Mesh scene;
    for (const Part& part : parts) {
        Mesh piece = partMesh(part, smoothProfile, n);
        int offset = static_cast<int>(scene.vertices.size());
        scene.vertices.insert(scene.vertices.end(), piece.vertices.begin(), piece.vertices.end());
        scene.normals.insert(scene.normals.end(), piece.normals.begin(), piece.normals.end());
        int flatOffset = static_cast<int>(scene.flatNormals.size());
        scene.flatNormals.insert(scene.flatNormals.end(), piece.flatNormals.begin(),
                                 piece.flatNormals.end());
        for (Face f : piece.faces) {
            if (f.flatNormal >= 0) {
                f.flatNormal += flatOffset;
            }
            scene.faces.push_back({offset + f.a, offset + f.b, offset + f.c, f.flatNormal});
        }
    }
    return scene;
}

// 장면 상태와 예제 모델
Part vaseBody() {
    Part body;
    body.profile = vase;
    body.capStart = true;
    return body;
}

std::vector<Part> parts{vaseBody()};
int active = 0;
Part& current() {
    return parts[active];
}

enum Command {
    New = 101,
    Vase,
    Save,
    Wire,
    Light,
    Smooth,
    Fewer,
    More,
    HandleDemo,
    AddAxis,
    AddRing,
    PlaceAxis,
    NextPart,
    PrevPart,
    RemovePart,
    TiltUp,
    TiltDown,
    Forward,
    Backward,
    Guides,
    AddSweep,
    SweepDemo,
    Thinner,
    Thicker,
    CloseProfile,
    CapStart
};

enum class Placement { None, Profile, Ring, Move };
Placement placement = Placement::None;
bool hasOrigin = false, guides = true;
Vec draftOrigin{}, pointerWorld{};
double sceneExtent = 1.16;
double sketchExtent = 1.16; // Keep the front-view grid still while the user drags the path.
HMENU axisMenu{}, profileMenu{};
HWND windowHandle{};
HDC dc{};
HGLRC context{};
GLuint fontBase{};
int width = 1000, height = 640, selected = -1, lastX = 0, lastY = 0;
bool editing = false, orbiting = false;
double yaw = -22, pitch = 12;
const int header = 48, footer = 59;
int half() {
    return width / 2;
}

int paneHeight() {
    return std::max(1, height - header - footer);
}

double scale() {
    return std::max(1.0, std::min((half() - 50) / 1.15, (paneHeight() - 20) / 2.0));
}

double axisX() {
    return 26;
}

double centerY() {
    return header + paneHeight() / 2.0;
}

double px(Point p) {
    return axisX() + scale() * p.r;
}

double py(Point p) {
    return centerY() - scale() * p.h;
}

void repaint() {
    InvalidateRect(windowHandle, nullptr, FALSE);
}

void rebuild() {
    curve = current().sweep ? sweepPathSamples(current().path, smooth)
                            : sampleProfile(current().profile, smooth, current().closeProfile);
    mesh = sceneMesh(parts, smooth, divisions);
    sceneExtent = 1.16;
    auto includePoint = [](Vec p) {
        sceneExtent = std::max(sceneExtent, 1.08 * std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
    };
    for (auto v : mesh.vertices) {
        includePoint(v);
    }
    for (const Part& part : parts) {
        includePoint(part.origin);
        for (Point p : part.path) {
            includePoint(toWorld(part, {p.r, p.h, 0}));
        }
    }
    repaint();
}
// OpenGL 화면 출력 ─ 왼쪽 단면 편집기와 오른쪽 3D 미리보기.
void pixelView() {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
}

void label(int x, int y, const std::string& s) {
    glRasterPos2i(x, y);
    glListBase(fontBase);
    glCallLists(static_cast<GLsizei>(s.size()), GL_UNSIGNED_BYTE, s.data());
}

void line(double x, double y, double x2, double y2) {
    glBegin(GL_LINES);
    glVertex2d(x, y);
    glVertex2d(x2, y2);
    glEnd();
}

void drawProfile() {
    const auto& profile = current().profile;
    glColor3f(.095f, .12f, .16f);
    glRecti(0, header, half() - 1, height - footer);
    for (int i = 0; i <= 5; ++i) {
        double x = px({i * .2, 0});
        glColor3f(.17f, .21f, .26f);
        line(x, header, x, height - footer);
    }
    for (int i = -5; i <= 5; ++i) {
        double y = py({0, i * .2});
        glColor3f(.17f, .21f, .26f);
        line(0, y, half() - 1, y);
    }
    glColor3f(.48f, .55f, .60f);
    line(axisX(), header, axisX(), height - footer);
    glColor3f(.80f, .89f, .94f);
    glLineWidth(2);
    if (current().ring) {
        // The two handles in the local editor set the ring radius and tube thickness.
        Point center{current().radius, 0};
        glBegin(GL_LINE_LOOP);
        for (int j = 0; j < 48; ++j) {
            double a = tau * j / 48;
            Point p{center.r + current().thickness * std::cos(a),
                    current().thickness * std::sin(a)};
            glVertex2d(px(p), py(p));
        }
        glEnd();
        glLineWidth(1);
        glPointSize(9);
        glBegin(GL_POINTS);
        glColor3f(.18f, .81f, .93f);
        glVertex2d(px(center), py(center));
        glColor3f(1, .67f, .27f);
        glVertex2d(px({center.r + current().thickness, 0}), py(center));
        glEnd();
        return;
    }
    glBegin(GL_LINE_STRIP);
    for (auto p : curve) {
        glVertex2d(px(p), py(p));
    }
    glEnd();
    glLineWidth(1);
    glPointSize(8);
    glBegin(GL_POINTS);
    for (int i = 0; i < (int)profile.size(); ++i) {
        if (i == selected) {
            glColor3f(1, .68f, .27f);
        } else {
            glColor3f(.15f, .78f, .93f);
        }
        glVertex2d(px(profile[i]), py(profile[i]));
    }
    glEnd();
}
// During placement the left pane switches from local (radius,height) to world XY.
double frontScale() {
    double extent = current().sweep && placement == Placement::None ? sketchExtent : sceneExtent;
    return std::max(1, std::min(half() - 24, paneHeight() - 24)) / (2 * extent);
}

Vec frontPoint(int x, int y) {
    return {(x - half() * .5) / frontScale(), (centerY() - y) / frontScale(), 0};
}

void frontVertex(Vec v) {
    glVertex2d(half() * .5 + v.x * frontScale(), centerY() - v.y * frontScale());
}

void drawFrontModel() {
    glColor3f(.095f, .12f, .16f);
    glRecti(0, header, half() - 1, height - footer);
    glColor3f(.22f, .27f, .33f);
    line(0, centerY(), half() - 1, centerY());
    line(half() * .5, header, half() * .5, height - footer);
    // Outline the existing model in front view to help place the new axis.
    glColor3f(.35f, .41f, .48f);
    glBegin(GL_TRIANGLES);
    for (Face f : mesh.faces) {
        frontVertex(mesh.vertices[f.a]);
        frontVertex(mesh.vertices[f.b]);
        frontVertex(mesh.vertices[f.c]);
    }
    glEnd();
}

void drawSweepEditor() {
    drawFrontModel();
    glColor3f(.35f, .84f, .90f);
    glLineWidth(2);
    glBegin(GL_LINE_STRIP);
    for (Point p : curve) {
        frontVertex({p.r, p.h, 0});
    }
    glEnd();
    glLineWidth(1);
    glPointSize(9);
    glBegin(GL_POINTS);
    for (int i = 0; i < (int)current().path.size(); ++i) {
        if (i == selected) {
            glColor3f(1, .69f, .32f);
        } else {
            glColor3f(.16f, .84f, .91f);
        }
        Point p = current().path[i];
        frontVertex({p.r, p.h, 0});
    }
    glEnd();
}

void drawPlacement() {
    drawFrontModel();
    for (int i = 0; i < (int)parts.size(); ++i) {
        const Part& p = parts[i];
        if (p.sweep) {
            continue;
        }
        glColor3f(.24f, .66f, .74f);
        glBegin(GL_LINES);
        frontVertex(toWorld(p, {0, -.35, 0}));
        frontVertex(toWorld(p, {0, .35, 0}));
        glEnd();
        glPointSize(6);
        glBegin(GL_POINTS);
        frontVertex(p.origin);
        glEnd();
    }
    if (hasOrigin) {
        glColor3f(1, .65f, .24f);
        glPointSize(9);
        glBegin(GL_POINTS);
        frontVertex(draftOrigin);
        glEnd();
        glBegin(GL_LINES);
        frontVertex(draftOrigin);
        frontVertex(pointerWorld);
        glEnd();
    }
}

void drawObject() {
    int w = std::max(1, width - half()), h = paneHeight();
    glViewport(half(), footer, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double aspect = double(w) / h;
    double extent = sceneExtent;
    double sx = extent * std::max(1.0, aspect), sy = extent * std::max(1.0, 1 / aspect);
    glOrtho(-sx, sx, -sy, sy, -2 * extent, 2 * extent);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const GLfloat pos[] = {-.4f, .8f, 1.f, 0}, spec[] = {.52f, .54f, .60f, 1};
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glRotatef((float)pitch, 1, 0, 0);
    glRotatef((float)yaw, 0, 1, 0);
    if (wire) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glColor3f(.48f, .64f, .74f);
    } else {
        glEnable(GL_DEPTH_TEST);
        if (light) {
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            glEnable(GL_COLOR_MATERIAL);
            glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 44);
        }
        glColor3f(.66f, .75f, .89f);
    }
    glBegin(GL_TRIANGLES);
    for (auto f : mesh.faces) {
        for (int i : {f.a, f.b, f.c}) {
            Vec n = f.flatNormal >= 0 ? mesh.flatNormals[f.flatNormal] : mesh.normals[i];
            Vec v = mesh.vertices[i];
            glNormal3d(n.x, n.y, n.z);
            glVertex3d(v.x, v.y, v.z);
        }
    }
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LIGHTING);
    if (wire) {
        glColor3f(.18f, .86f, 1);
        glPointSize(3);
        glBegin(GL_POINTS);
        for (auto v : mesh.vertices) {
            glVertex3d(v.x, v.y, v.z);
        }
        glEnd();
    }
}

void drawAxisGuides() {
    if (!guides) {
        return;
    }
    // The preview already has its orbit and projection matrices loaded.
    glDisable(GL_DEPTH_TEST);
    for (int i = 0; i < (int)parts.size(); ++i) {
        const Part& part = parts[i];
        if (part.sweep) {
            continue;
        }
        if (i == active) {
            glColor3f(1, .65f, .25f);
        } else {
            glColor3f(.25f, .57f, .63f);
        }
        Vec a = toWorld(part, {0, -.32, 0}), b = toWorld(part, {0, .36, 0});
        glBegin(GL_LINES);
        glVertex3d(a.x, a.y, a.z);
        glVertex3d(b.x, b.y, b.z);
        glEnd();
        glPointSize(5);
        glBegin(GL_POINTS);
        glVertex3d(part.origin.x, part.origin.y, part.origin.z);
        glEnd();
    }
}

void draw() {
    glClearColor(.065f, .078f, .10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pixelView();
    if (placement != Placement::None) {
        drawPlacement();
    } else if (current().sweep) {
        drawSweepEditor();
    } else {
        drawProfile();
    }
    drawObject();
    drawAxisGuides();
    pixelView();
    glColor3f(.9f, .94f, .98f);
    std::string title = "AXIS " + std::to_string(active + 1) + " / " + std::to_string(parts.size());
    if (placement != Placement::None) {
        title = "PLACE AXIS  /  FRONT VIEW (XY)";
    } else if (current().sweep) {
        title = "SWEEP PATH  /  FRONT VIEW (XY)";
    } else {
        title += current().ring ? "    RING CROSS-SECTION" : "    LOCAL PROFILE";
    }
    label(16, 22, title);
    label(half() + 16, 22, "MODEL  /  3D");
    glColor3f(.62f, .70f, .76f);
    if (placement != Placement::None) {
        label(16, 40,
              hasOrigin ? "Click a direction from the center.  Esc: cancel"
                        : "Click the axis center on the model.  Esc: cancel");
    } else if (current().sweep) {
        label(16, 40, "Click in order to extend. Drag a point to edit. Right-click to delete.");
    } else if (current().ring) {
        label(16, 40, "Drag blue for ring size, orange for tube thickness.");
    } else {
        label(16, 40, "Click: append   Shift: level   Ctrl+click: force new point");
    }
    label(half() + 16, 40, "Drag: orbit    W: wireframe    L: lighting");
    std::ostringstream info;
    info << "N = " << divisions << "     Vertices: " << mesh.vertices.size()
         << "     Triangles: " << mesh.faces.size();
    if (!current().sweep && !current().ring) {
        info << (current().closeProfile ? "     Closed profile" : "     Open profile")
             << "     Drag: move   Right-click: delete";
    }
    label(16, height - 41, info.str());
    std::ostringstream where;
    if (current().sweep) {
        where << "Sweep " << active + 1 << "    Points: " << current().path.size()
              << "    Tube radius: " << std::fixed << std::setprecision(3) << current().tubeRadius
              << "     [ / ]: thinner / thicker";
    } else {
        where << std::fixed << std::setprecision(0) << "Axis " << active + 1 << "   XY angle "
              << current().angle << " deg     Depth tilt " << current().tilt << " deg";
    }
    where << "     Z " << std::fixed << std::setprecision(2) << current().origin.z
          << "     Tab: next part";
    label(16, height - 24, where.str());
    label(16, height - 7,
          "A: add axis    H: ring    S: sweep path    M: place axis    +/-: divisions    C: smooth "
          "curve    Ctrl+S: export OBJ");
    SwapBuffers(dc);
}
// OBJ 저장 ─ 화면과 동일한 정점, 삼각형, 법선을 내보낸다.
bool writeObj(const std::filesystem::path& path, const Mesh& m) {
    std::ofstream out(path);
    out << std::setprecision(17) << "o SurfaceOfRevolution\n";
    for (auto p : m.vertices) {
        out << "v " << p.x << " " << p.y << " " << p.z << "\n";
    }
    for (auto p : m.normals) {
        out << "vn " << p.x << " " << p.y << " " << p.z << "\n";
    }
    for (auto p : m.flatNormals) {
        out << "vn " << p.x << " " << p.y << " " << p.z << "\n";
    }
    for (auto f : m.faces) {
        out << "f";
        for (int i : {f.a, f.b, f.c}) {
            size_t normal =
                f.flatNormal >= 0 ? m.normals.size() + f.flatNormal : static_cast<size_t>(i);
            out << " " << i + 1 << "//" << normal + 1;
        }
        out << "\n";
    }
    out.close();
    return !out.fail();
}

void exportMesh() {
    if (mesh.faces.empty()) {
        MessageBoxW(windowHandle, L"Add at least two profile points.", L"Export OBJ", MB_OK);
        return;
    }
    wchar_t name[MAX_PATH] = L"sor_model.obj";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = windowHandle;
    dialog.lpstrFile = name;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"obj";
    dialog.lpstrFilter = L"Wavefront OBJ (*.obj)\0*.obj\0\0";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }
    wchar_t temp[MAX_PATH];
    auto folder = std::filesystem::path(name).parent_path();
    if (!GetTempFileNameW(folder.c_str(), L"sor", 0, temp)) {
        MessageBoxW(windowHandle, L"Cannot create file in this folder.", L"Export OBJ", MB_OK);
        return;
    }
    if (!writeObj(temp, mesh) ||
        !MoveFileExW(temp, name, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp);
        MessageBoxW(windowHandle, L"Could not save the OBJ file.", L"Export OBJ",
                    MB_OK | MB_ICONERROR);
    }
}
// 부품 선택, 축 배치, 메뉴 명령
void selectPart(int index) {
    active = index;
    selected = -1;
    sketchExtent = sceneExtent;
    placement = Placement::None;
    hasOrigin = false;
    editing = false;
    orbiting = false;
    if (GetCapture() == windowHandle) {
        ReleaseCapture();
    }
}

void startPlacement(Placement mode) {
    selectPart(active);
    placement = mode;
    repaint();
}

void placeClick(int x, int y) {
    Vec point = frontPoint(x, y);
    if (!hasOrigin) {
        draftOrigin = pointerWorld = point;
        hasOrigin = true;
        repaint();
        return;
    }
    Vec delta = point - draftOrigin;
    if (std::hypot(delta.x, delta.y) * frontScale() < 6) {
        return;
    }
    if (placement != Placement::Move) {
        Part part;
        part.ring = (placement == Placement::Ring);
        if (part.ring) {
            part.tilt = 65; // Show the opening from the front.
        }
        parts.push_back(part);
        active = (int)parts.size() - 1;
    }
    current().origin.x = draftOrigin.x;
    current().origin.y = draftOrigin.y;
    current().angle = std::atan2(delta.x, delta.y) * 360 / tau;
    selectPart(active);
    rebuild();
}

constexpr int axisListStart = 1000;
void updateAxisList() {
    // Keep a direct selection entry for every part, including empty profiles.
    int count = GetMenuItemCount(axisMenu);
    while (count > 0 && GetMenuItemID(axisMenu, count - 1) >= axisListStart) {
        DeleteMenu(axisMenu, --count, MF_BYPOSITION);
    }
    for (int i = 0; i < (int)parts.size(); ++i) {
        std::wstring name = parts[i].sweep ? L"Sweep " + std::to_wstring(i + 1)
                                           : L"Axis " + std::to_wstring(i + 1) +
                                                 (parts[i].ring ? L"  (ring)" : L"  (profile)");
        AppendMenuW(axisMenu, MF_STRING | (active == i ? MF_CHECKED : 0), axisListStart + i,
                    name.c_str());
    }
    EnableMenuItem(axisMenu, RemovePart,
                   MF_BYCOMMAND | (parts.size() > 1 ? MF_ENABLED : MF_GRAYED));
    for (int id : {PlaceAxis, TiltUp, TiltDown}) {
        EnableMenuItem(axisMenu, id, MF_BYCOMMAND | (current().sweep ? MF_GRAYED : MF_ENABLED));
    }
    for (int id : {Thinner, Thicker}) {
        EnableMenuItem(axisMenu, id, MF_BYCOMMAND | (current().sweep ? MF_ENABLED : MF_GRAYED));
    }
}

void command(int id) {
    if (id >= axisListStart && id - axisListStart < (int)parts.size()) {
        selectPart(id - axisListStart);
        rebuild();
        return;
    }
    switch (id) {
        case New:
            selectPart(active);
            if (current().sweep) {
                current().path.clear();
            } else {
                current().profile.clear();
                current().ring = false;
                current().capStart = false;
                current().closeProfile = false;
            }
            break;
        case Vase:
            parts = {vaseBody()};
            selectPart(0);
            break;
        case HandleDemo: {
            Part handle;
            handle.ring = true;
            handle.origin = {.55, .15, 0};
            handle.angle = 90;
            handle.tilt = 72;
            parts = {vaseBody(), handle};
            selectPart(1);
            break;
        }
        case AddAxis:
            startPlacement(Placement::Profile);
            return;
        case AddRing:
            startPlacement(Placement::Ring);
            return;
        case AddSweep: {
            Part handle;
            handle.sweep = true;
            parts.push_back(handle);
            selectPart((int)parts.size() - 1);
            break;
        }
        case SweepDemo: {
            Part handle;
            handle.sweep = true;
            handle.path = {{.245, .38}, {.60, .45},  {.82, .35}, {.88, .12},
                           {.80, -.12}, {.60, -.26}, {.40, -.31}};
            parts = {vaseBody(), handle};
            selectPart(1);
            rebuild();
            sketchExtent = sceneExtent;
            return;
        }
        case Thinner:
        case Thicker:
            if (!current().sweep) {
                return;
            }
            current().tubeRadius =
                std::clamp(current().tubeRadius + (id == Thicker ? .005 : -.005), .01, .20);
            break;
        case PlaceAxis:
            if (!current().sweep) {
                startPlacement(Placement::Move);
            }
            return;
        case NextPart:
            selectPart((active + 1) % (int)parts.size());
            break;
        case PrevPart:
            selectPart((active + (int)parts.size() - 1) % (int)parts.size());
            break;
        case RemovePart:
            if (parts.size() > 1) {
                parts.erase(parts.begin() + active);
                selectPart(std::min(active, (int)parts.size() - 1));
            }
            break;
        case TiltUp:
        case TiltDown:
            if (current().sweep) {
                return;
            }
            current().tilt = std::clamp(current().tilt + (id == TiltUp ? 5 : -5), -90.0, 90.0);
            break;
        case Forward:
            current().origin.z += .05;
            break;
        case Backward:
            current().origin.z -= .05;
            break;
        case CloseProfile:
            if (current().ring || current().sweep || placement != Placement::None) {
                return;
            }
            current().closeProfile = !current().closeProfile;
            break;
        case CapStart:
            if (current().ring || current().sweep || current().closeProfile ||
                placement != Placement::None) {
                return;
            }
            current().capStart = !current().capStart;
            break;
        case Guides:
            guides = !guides;
            break;
        case Save:
            exportMesh();
            return;
        case Wire:
            wire = !wire;
            break;
        case Light:
            light = !light;
            break;
        case Smooth:
            smooth = !smooth;
            break;
        case Fewer:
            divisions = std::max(3, divisions - 1);
            break;
        case More:
            divisions = std::min(180, divisions + 1);
            break;
        default:
            return;
    }
    HMENU menu = GetMenu(windowHandle);
    for (auto entry :
         {std::pair<int, bool>{Wire, wire}, {Light, light}, {Smooth, smooth}, {Guides, guides}}) {
        CheckMenuItem(menu, entry.first, MF_BYCOMMAND | (entry.second ? MF_CHECKED : MF_UNCHECKED));
    }
    DrawMenuBar(windowHandle);
    rebuild();
}
// 마우스 입력 ─ 단면 점 선택, 추가, 이동, 삭제.
int hit(int x, int y) {
    const auto& profile = current().profile;
    double best = 11;
    int index = -1;
    for (int i = 0; i < (int)profile.size(); ++i) {
        double d = std::hypot(px(profile[i]) - x, py(profile[i]) - y);
        if (d < best) {
            best = d;
            index = i;
        }
    }
    return index;
}

Point mousePoint(int x, int y) {
    double r = std::clamp((x - axisX()) / scale(), 0.0, 1.0);
    if (x <= axisX() + 4) {
        r = 0;
    }
    return {r, std::clamp((centerY() - y) / scale(), -.96, .96)};
}

void editSweepPoint(int x, int y, bool right) {
    auto& path = current().path;
    Vec p = frontPoint(x, y);
    double best = 10 / frontScale(); // Hit tolerance is ten pixels at every zoom level.
    selected = -1;
    for (int i = 0; i < (int)path.size(); ++i) {
        double distance = std::hypot(p.x - path[i].r, p.y - path[i].h);
        if (distance < best) {
            best = distance;
            selected = i;
        }
    }
    if (right) {
        if (selected >= 0) {
            path.erase(path.begin() + selected);
        }
        selected = -1;
    } else {
        if (selected < 0) {
            path.push_back({p.x, p.y});
            selected = (int)path.size() - 1;
        }
        editing = true;
        SetCapture(windowHandle);
    }
    rebuild();
}
// 일반 클릭은 선택/추가, Ctrl+클릭은 좁은 테두리에서도 새 점을 추가한다.
// Shift를 누르면 바로 앞 점의 높이에 정확히 맞춘다. 첫 점 편집 시에는 둘째 점이 기준이다.
Point profileMousePoint(int x, int y, const std::vector<Point>& points, size_t index, bool level) {
    Point point = mousePoint(x, y);
    if (level && index > 0) {
        point.h = points[index - 1].h;
    } else if (level && points.size() > 1) {
        point.h = points[1].h;
    }
    return point;
}

void mouseDown(int x, int y, bool right, WPARAM mouseKeys) {
    if (y < header || y >= height - footer) {
        return;
    }
    if (x >= half()) {
        if (!right) {
            orbiting = true;
            lastX = x;
            lastY = y;
            SetCapture(windowHandle);
        }
        return;
    }
    if (placement != Placement::None) {
        if (right) {
            selectPart(active);
            repaint();
        } else {
            placeClick(x, y);
        }
        return;
    }
    if (current().sweep) {
        editSweepPoint(x, y, right);
        return;
    }
    if (current().ring) {
        if (right || std::abs(y - centerY()) > 12) {
            return;
        }
        double middle = px({current().radius, 0});
        double edge = px({current().radius + current().thickness, 0});
        double a = std::abs(x - middle), b = std::abs(x - edge);
        if (std::min(a, b) <= 12) {
            selected = a <= b ? 0 : 1;
            editing = true;
            SetCapture(windowHandle);
        }
        return;
    }
    auto& profile = current().profile;
    selected = (!right && (mouseKeys & MK_CONTROL)) ? -1 : hit(x, y);
    if (right) {
        if (selected >= 0) {
            profile.erase(profile.begin() + selected);
            selected = -1;
            rebuild();
        }
        return;
    }
    if (selected < 0) {
        Point p = profileMousePoint(x, y, profile, profile.size(), (mouseKeys & MK_SHIFT) != 0);
        if (!profile.empty() && samePoint(profile.back(), p)) {
            return;
        }
        selected = static_cast<int>(profile.size());
        profile.push_back(p);
    }
    editing = true;
    SetCapture(windowHandle);
    rebuild();
}

void mouseMove(int x, int y, WPARAM mouseKeys) {
    if (placement != Placement::None && hasOrigin && x < half() && !orbiting) {
        pointerWorld =
            frontPoint(std::clamp(x, 0, half() - 1), std::clamp(y, header, height - footer));
        repaint();
        return;
    }
    if (editing && selected >= 0) {
        if (current().sweep) {
            Vec p = frontPoint(std::clamp(x, 0, half() - 1),
                               std::clamp(y, header, height - footer - 1));
            current().path[selected] = {p.x, p.y};
            rebuild();
            return;
        }
        if (current().ring) {
            Point p = mousePoint(x, y);
            if (selected == 0) {
                current().radius = std::clamp(p.r, current().thickness + .02, .85);
            } else {
                current().thickness = std::clamp(p.r - current().radius, .015,
                                                 std::min(.25, current().radius - .015));
            }
            rebuild();
            return;
        }
        auto& profile = current().profile;
        profile[selected] = profileMousePoint(x, y, profile, selected, (mouseKeys & MK_SHIFT) != 0);
        rebuild();
    } else if (orbiting) {
        yaw += .6 * (x - lastX);
        pitch = std::clamp(pitch + .5 * (y - lastY), -89.0, 89.0);
        lastX = x;
        lastY = y;
        repaint();
    }
}
// Windows 창, OpenGL 컨텍스트와 이벤트 루프
bool initGL(HWND hwnd) {
    dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR fmt{};
    fmt.nSize = sizeof(fmt);
    fmt.nVersion = 1;
    fmt.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    fmt.iPixelType = PFD_TYPE_RGBA;
    fmt.cColorBits = 24;
    fmt.cDepthBits = 24;
    int pf = ChoosePixelFormat(dc, &fmt);
    if (!pf || !SetPixelFormat(dc, pf, &fmt)) {
        return false;
    }
    context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) {
        return false;
    }
    auto old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    fontBase = glGenLists(128);
    BOOL ok = wglUseFontBitmapsA(dc, 0, 128, fontBase);
    SelectObject(dc, old);
    return ok != FALSE;
}

LRESULT CALLBACK events(HWND hwnd, UINT msg, WPARAM key, LPARAM arg) {
    switch (msg) {
        case WM_CREATE:
            windowHandle = hwnd;
            if (!initGL(hwnd)) {
                MessageBoxW(hwnd, L"Could not initialize Windows OpenGL.", L"SOR Modeler", MB_OK);
                return -1;
            }
            rebuild();
            return 0;
        case WM_SIZE:
            width = std::max(1, (int)LOWORD(arg));
            height = std::max(1, (int)HIWORD(arg));
            repaint();
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(arg)->ptMinTrackSize = {740, 450};
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (context) {
                draw();
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_INITMENUPOPUP:
            if (axisMenu && (HMENU)key == axisMenu) {
                updateAxisList();
            }
            if (profileMenu && (HMENU)key == profileMenu) {
                bool editable = !current().ring && !current().sweep && placement == Placement::None;
                EnableMenuItem(profileMenu, CloseProfile,
                               MF_BYCOMMAND | (editable ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(profileMenu, CapStart,
                               MF_BYCOMMAND |
                                   (editable && !current().closeProfile ? MF_ENABLED : MF_GRAYED));
                CheckMenuItem(profileMenu, CloseProfile,
                              MF_BYCOMMAND |
                                  (editable && current().closeProfile ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(profileMenu, CapStart,
                              MF_BYCOMMAND |
                                  (editable && !current().closeProfile && current().capStart
                                       ? MF_CHECKED
                                       : MF_UNCHECKED));
            }
            return 0;
        case WM_COMMAND:
            command(LOWORD(key));
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            mouseDown(GET_X_LPARAM(arg), GET_Y_LPARAM(arg), msg == WM_RBUTTONDOWN, key);
            return 0;
        case WM_MOUSEMOVE:
            mouseMove(GET_X_LPARAM(arg), GET_Y_LPARAM(arg), key);
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        case WM_CAPTURECHANGED:
            editing = orbiting = false;
            return 0;
        case WM_KEYDOWN:
            switch (key) {
                case VK_ESCAPE:
                    selectPart(active);
                    repaint();
                    break;
                case VK_TAB:
                    command(GetKeyState(VK_SHIFT) < 0 ? PrevPart : NextPart);
                    break;
                case 'A':
                    command(AddAxis);
                    break;
                case 'H':
                    command(AddRing);
                    break;
                case 'M':
                    command(PlaceAxis);
                    break;
                case 'G':
                    command(Guides);
                    break;
                case VK_UP:
                    command(TiltUp);
                    break;
                case VK_DOWN:
                    command(TiltDown);
                    break;
                case VK_PRIOR:
                    command(Forward);
                    break;
                case VK_NEXT:
                    command(Backward);
                    break;
                case 'W':
                    command(Wire);
                    break;
                case 'C':
                    command(Smooth);
                    break;
                case 'K':
                    if (!(arg & (1L << 30))) {
                        command(CloseProfile);
                    }
                    break;
                case 'L':
                    command(Light);
                    break;
                case 'S':
                    if (!(arg & (1L << 30))) {
                        command(GetKeyState(VK_CONTROL) < 0 ? Save : AddSweep);
                    }
                    break;
                case VK_OEM_4:
                    command(Thinner);
                    break;
                case VK_OEM_6:
                    command(Thicker);
                    break;
                case VK_ADD:
                case VK_OEM_PLUS:
                    command(More);
                    break;
                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    command(Fewer);
                    break;
                case VK_DELETE:
                    if (!current().ring && selected >= 0 && placement == Placement::None) {
                        auto& points = current().sweep ? current().path : current().profile;
                        points.erase(points.begin() + selected);
                        selected = -1;
                        rebuild();
                    }
                    break;
            }
            return 0;
        case WM_DESTROY:
            wglMakeCurrent(nullptr, nullptr);
            if (context) {
                wglDeleteContext(context);
            }
            if (dc) {
                ReleaseDC(hwnd, dc);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, key, arg);
}

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = events;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SORModelerGL";
    if (!RegisterClassW(&wc)) {
        return 1;
    }
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), display = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, New, L"Clear selected profile or path");
    AppendMenuW(file, MF_STRING, Vase, L"Load example: vase");
    AppendMenuW(file, MF_STRING, HandleDemo, L"Load example: vase with handle");
    AppendMenuW(file, MF_STRING, SweepDemo, L"Load example: vase with D-handle (sweep)");
    AppendMenuW(file, MF_STRING, Save, L"Export OBJ...\tCtrl+S");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"File");
    profileMenu = CreatePopupMenu();
    AppendMenuW(profileMenu, MF_STRING, CloseProfile, L"Join last point to first\tK");
    AppendMenuW(profileMenu, MF_STRING, CapStart, L"Fill start ring to axis");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)profileMenu, L"Profile");
    axisMenu = CreatePopupMenu();
    AppendMenuW(axisMenu, MF_STRING, AddAxis, L"Add profile axis...\tA");
    AppendMenuW(axisMenu, MF_STRING, AddRing, L"Add ring handle...\tH");
    AppendMenuW(axisMenu, MF_STRING, AddSweep, L"Draw sweep handle...\tS");
    AppendMenuW(axisMenu, MF_STRING, Thinner, L"Thinner sweep tube\t[");
    AppendMenuW(axisMenu, MF_STRING, Thicker, L"Thicker sweep tube\t]");
    AppendMenuW(axisMenu, MF_STRING, PlaceAxis, L"Reposition selected axis...\tM");
    AppendMenuW(axisMenu, MF_STRING, TiltUp, L"Tilt toward +Z (5 deg)\tUp");
    AppendMenuW(axisMenu, MF_STRING, TiltDown, L"Tilt toward -Z (5 deg)\tDown");
    AppendMenuW(axisMenu, MF_STRING, Forward, L"Move toward +Z\tPage Up");
    AppendMenuW(axisMenu, MF_STRING, Backward, L"Move toward -Z\tPage Down");
    AppendMenuW(axisMenu, MF_STRING, NextPart, L"Select next part\tTab");
    AppendMenuW(axisMenu, MF_STRING, RemovePart, L"Remove selected part");
    AppendMenuW(axisMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)axisMenu, L"Axes / Parts");
    AppendMenuW(display, MF_STRING, Wire, L"Triangle wireframe + vertices\tW");
    AppendMenuW(display, MF_CHECKED, Light, L"Lighting\tL");
    AppendMenuW(display, MF_CHECKED, Smooth, L"Smooth profiles and paths\tC");
    AppendMenuW(display, MF_CHECKED, Guides, L"Show axes on composite model\tG");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)display, L"View");
    AppendMenuW(menu, MF_STRING, Fewer, L"Divisions -");
    AppendMenuW(menu, MF_STRING, More, L"Divisions +");
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"SOR Modeler", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, 1000, 650, nullptr, menu, instance, nullptr);
    if (!hwnd) {
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
