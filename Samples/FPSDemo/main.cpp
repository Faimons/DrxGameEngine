/**
 * DRX Engine - FPS 3D Shooter
 * Simplified rendering with immediate mode
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <GLFW/glfw3.h>

// ============================================================================
// Simple Vector/Matrix Math
// ============================================================================

struct Vec3 {
    float x, y, z;
    Vec3(float x=0, float y=0, float z=0) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& v) const { return Vec3(x+v.x, y+v.y, z+v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x-v.x, y-v.y, z-v.z); }
    Vec3 operator*(float f) const { return Vec3(x*f, y*f, z*f); }
    float dot(const Vec3& v) const { return x*v.x + y*v.y + z*v.z; }
    float len() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 norm() const { float l = len(); return l > 0 ? Vec3(x/l, y/l, z/l) : Vec3(); }
};

// ============================================================================
// Game Data
// ============================================================================

struct Enemy {
    Vec3 p;
    float h = 100;
    bool alive = true;
    float t = 0;

    void Upd(Vec3 pp, float dt) {
        if(!alive) return;
        Vec3 to = pp - p;
        float d = to.len();
        if(d < 30 && d > 0.1f) {
            p = p + to.norm() * 2.0f * dt;
            if(d < 2) { t -= dt; if(t <= 0) {t = 1; } }
        }
    }

    bool Hit(float dmg) {
        h -= dmg;
        if(h <= 0) alive = false;
        return h <= 0;
    }
};

struct Weap {
    const char*n;
    float dmg, fr, ammo, mxammo, reload;
    bool CanF() const { return reload <= 0 && ammo > 0; }
    void Fire() { if(CanF()) { ammo--; reload = 1.0f/fr; } }
    void Upd(float dt) { if(reload > 0) reload -= dt; }
};

struct Player {
    Vec3 p = Vec3(0, 1.7f, 0);
    float yaw = 0, pitch = 0, h = 100;
    int score = 0, wave = 1, kills = 0;
    std::vector<Weap> w;
    int cw = 0;

    Player() {
        w.push_back({"Rifle", 25, 10, 300, 300, 0});
        w.push_back({"Pistol", 35, 5, 100, 100, 0});
        w.push_back({"Shotgun", 60, 2, 50, 50, 0});
    }

    void Upd(float dt) { if(cw < w.size()) w[cw].Upd(dt); }

    Vec3 Look() const {
        float ry = yaw * 3.14159f / 180, rp = pitch * 3.14159f / 180;
        return Vec3(std::sin(ry)*std::cos(rp), std::sin(rp), std::cos(ry)*std::cos(rp)).norm();
    }
};

struct Game {
    Player pl;
    std::vector<Enemy> en;
    int ens = 5;
    float st = 0;
    bool over = false;

    void Spawn() {
        if(ens > 0) {
            st -= 0.016f;
            if(st <= 0) {
                float a = (rand()%360) * 3.14159f / 180;
                float d = 20 + rand()%10;
                Enemy e;
                e.p = Vec3(pl.p.x + std::cos(a)*d, 0.5f, pl.p.z + std::sin(a)*d);
                en.push_back(e);
                ens--;
                st = 2.0f;
            }
        }
    }

    void UpdE(float dt) {
        for(auto&e:en) e.Upd(pl.p, dt);
        for(auto it=en.begin();it!=en.end();) {
            if(!it->alive) { pl.score+=100; pl.kills++; it=en.erase(it); }
            else ++it;
        }
    }

    void Coll() {
        for(auto&e:en) {
            if(e.alive && e.t <= 0) {
                float d = (pl.p - e.p).len();
                if(d < 2) { pl.h -= 5; e.t = 1; }
            }
        }
        if(pl.h <= 0) over = true;
    }

    void NextW() {
        if(en.empty() && ens == 0) {
            pl.wave++;
            ens = 5 + pl.wave*2;
            st = 2.0f;
        }
    }
};

struct Input {
    bool w, a, s, d;
    float mx, my;
    bool lc;
    int w1, w2, w3;
};

Input inp = {0};

void KeyCB(GLFWwindow*,int k,int,int a,int) {
    bool p = a==GLFW_PRESS || a==GLFW_REPEAT;
    if(k==GLFW_KEY_W) inp.w=p;
    if(k==GLFW_KEY_A) inp.a=p;
    if(k==GLFW_KEY_S) inp.s=p;
    if(k==GLFW_KEY_D) inp.d=p;
    if(k==GLFW_KEY_1) inp.w1=1;
    if(k==GLFW_KEY_2) inp.w2=1;
    if(k==GLFW_KEY_3) inp.w3=1;
}

void MouseCB(GLFWwindow*,int b,int a,int) {
    if(b==GLFW_MOUSE_BUTTON_LEFT) inp.lc=(a==GLFW_PRESS);
}

void MotionCB(GLFWwindow*,double x,double y) {
    static double lx=x, ly=y;
    inp.mx = (x-lx)*0.1f; inp.my = (ly-y)*0.1f;
    lx=x; ly=y;
}

// ============================================================================
// Rendering
// ============================================================================

void DrawCube(Vec3 pos, float size, float r, float g, float b) {
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glColor3f(r, g, b);

    float s = size * 0.5f;
    glBegin(GL_QUADS);
    // Front
    glVertex3f(-s,-s, s); glVertex3f( s,-s, s); glVertex3f( s, s, s); glVertex3f(-s, s, s);
    // Back
    glVertex3f(-s,-s,-s); glVertex3f(-s, s,-s); glVertex3f( s, s,-s); glVertex3f( s,-s,-s);
    // Left
    glVertex3f(-s,-s,-s); glVertex3f(-s,-s, s); glVertex3f(-s, s, s); glVertex3f(-s, s,-s);
    // Right
    glVertex3f( s,-s,-s); glVertex3f( s, s,-s); glVertex3f( s, s, s); glVertex3f( s,-s, s);
    // Top
    glVertex3f(-s, s,-s); glVertex3f(-s, s, s); glVertex3f( s, s, s); glVertex3f( s, s,-s);
    // Bottom
    glVertex3f(-s,-s,-s); glVertex3f( s,-s,-s); glVertex3f( s,-s, s); glVertex3f(-s,-s, s);
    glEnd();

    glPopMatrix();
}

void DrawPlane(Vec3 pos, float size, float r, float g, float b) {
    glPushMatrix();
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    float s = size;
    glVertex3f(pos.x-s, pos.y, pos.z-s);
    glVertex3f(pos.x+s, pos.y, pos.z-s);
    glVertex3f(pos.x+s, pos.y, pos.z+s);
    glVertex3f(pos.x-s, pos.y, pos.z+s);
    glEnd();
    glPopMatrix();
}

int main() {
    srand((unsigned)time(0));

    if(!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // Request OpenGL 2.1 for maximum compatibility
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "DRX 3D Shooter", nullptr, nullptr);
    if(!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, KeyCB);
    glfwSetMouseButtonCallback(window, MouseCB);
    glfwSetCursorPosCallback(window, MotionCB);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSwapInterval(1);

    // Setup OpenGL
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // Setup projection manually (without GLU)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float fov = 45.0f * 3.14159f / 180.0f;  // Convert to radians
    float aspect = 1280.0f / 720.0f;
    float f = 1.0f / std::tan(fov * 0.5f);
    float nearZ = 0.1f, farZ = 500.0f;

    float proj[16] = {
        f/aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (farZ+nearZ)/(nearZ-farZ), -1,
        0, 0, (2*farZ*nearZ)/(nearZ-farZ), 0
    };
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);

    Game g;
    std::cout << "DRX 3D SHOOTER\nControls: WASD move, Mouse look, Click shoot, 1/2/3 weapons, ESC quit\n";

    auto lt = std::chrono::high_resolution_clock::now();

    while(!glfwWindowShouldClose(window)) {
        auto ct = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(ct-lt).count();
        lt = ct;
        if(dt > 0.033f) dt = 0.033f;

        // Update
        Vec3 mv;
        if(inp.w) mv = mv + Vec3(0,0,1);
        if(inp.s) mv = mv + Vec3(0,0,-1);
        if(inp.a) mv = mv + Vec3(-1,0,0);
        if(inp.d) mv = mv + Vec3(1,0,0);
        if(mv.len() > 0) g.pl.p = g.pl.p + mv.norm() * 5.0f * dt;

        g.pl.yaw += inp.mx;
        g.pl.pitch += inp.my;
        inp.mx = 0; inp.my = 0;
        if(g.pl.pitch > 89) g.pl.pitch = 89;
        if(g.pl.pitch < -89) g.pl.pitch = -89;

        if(inp.w1) g.pl.cw = 0;
        if(inp.w2) g.pl.cw = 1;
        if(inp.w3) g.pl.cw = 2;
        inp.w1 = inp.w2 = inp.w3 = 0;

        if(inp.lc && !g.over) {
            auto& we = g.pl.w[g.pl.cw];
            if(we.CanF()) {
                we.Fire();
                Vec3 rd = g.pl.Look();
                float cd = 999;
                int he = -1;
                for(size_t i=0;i<g.en.size();i++) {
                    Vec3 te = g.en[i].p - g.pl.p;
                    float d = te.len();
                    if(d < 50 && d < cd) {
                        float ang = std::acos(te.norm().dot(rd));
                        if(ang < 0.2f) { cd = d; he = i; }
                    }
                }
                if(he >= 0) {
                    if(g.en[he].Hit(we.dmg)) { g.pl.score += 100; g.pl.kills++; }
                }
            }
        }

        g.pl.Upd(dt);
        g.Spawn();
        g.UpdE(dt);
        g.Coll();
        g.NextW();

        // Render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Setup camera manually (without gluLookAt)
        Vec3 eye = g.pl.p;
        Vec3 look = g.pl.Look();
        Vec3 at = eye + look;
        Vec3 up(0, 1, 0);

        Vec3 f = (at - eye).norm();
        Vec3 s = Vec3(f.y*up.z-f.z*up.y, f.z*up.x-f.x*up.z, f.x*up.y-f.y*up.x).norm();
        Vec3 u = Vec3(s.y*f.z-s.z*f.y, s.z*f.x-s.x*f.z, s.x*f.y-s.y*f.x);

        float cam[16] = {
            s.x, u.x, -f.x, 0,
            s.y, u.y, -f.y, 0,
            s.z, u.z, -f.z, 0,
            -s.dot(eye), -u.dot(eye), f.dot(eye), 1
        };
        glMultMatrixf(cam);

        // Draw ground
        DrawPlane(Vec3(0, -1, 0), 50, 0.2f, 0.6f, 0.2f);

        // Draw enemies
        for(auto& e : g.en) {
            if(e.alive) {
                DrawCube(e.p, 0.5f, 1.0f, 0.0f, 0.0f);
            }
        }

        // Text rendering (hud info to console)
        static int fc = 0;
        if(++fc % 30 == 0) {
            std::cout << "\rH:" << (int)g.pl.h << " S:" << g.pl.score
                      << " W:" << g.pl.wave << " E:" << g.en.size()
                      << " WP:" << g.pl.w[g.pl.cw].n << std::flush;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
        if(glfwGetKey(window, GLFW_KEY_ESCAPE)) break;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "\nGame Over! Score: " << g.pl.score << " Kills: " << g.pl.kills << "\n";
    return 0;
}
