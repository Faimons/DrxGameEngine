/**
 * DRX Engine - Battlefield-Style Demo
 * Full-featured FPS with procedural terrain, weapons, vehicles, and enemies
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <string>
#include <algorithm>
#include <GLFW/glfw3.h>

// ============================================================================
// Math Types
// ============================================================================

struct Vec3 {
    float x, y, z;
    Vec3(float x=0, float y=0, float z=0) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& v) const { return Vec3(x+v.x, y+v.y, z+v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x-v.x, y-v.y, z-v.z); }
    Vec3 operator*(float f) const { return Vec3(x*f, y*f, z*f); }
    float dot(const Vec3& v) const { return x*v.x + y*v.y + z*v.z; }
    Vec3 cross(const Vec3& v) const {
        return Vec3(y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x);
    }
    float len() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 norm() const { float l = len(); return l > 0 ? Vec3(x/l, y/l, z/l) : Vec3(); }
};

struct Color { float r, g, b; };

// ============================================================================
// Procedural Terrain
// ============================================================================

class Terrain {
public:
    static const int SIZE = 64;
    float heights[SIZE][SIZE];
    Color colors[SIZE][SIZE];
    float scale = 2.0f;

    void Generate(unsigned int seed) {
        srand(seed);
        // Multi-octave noise
        for (int z = 0; z < SIZE; z++) {
            for (int x = 0; x < SIZE; x++) {
                float h = 0;
                float amp = 1.0f;
                float freq = 0.05f;

                for (int oct = 0; oct < 4; oct++) {
                    h += Noise(x * freq, z * freq) * amp;
                    amp *= 0.5f;
                    freq *= 2.0f;
                }

                heights[x][z] = h * 8.0f;

                // Color based on height
                float nh = (h + 1) * 0.5f;
                if (nh < 0.3f) {
                    colors[x][z] = {0.2f, 0.5f, 0.2f}; // Grass
                } else if (nh < 0.6f) {
                    colors[x][z] = {0.4f, 0.35f, 0.25f}; // Dirt
                } else {
                    colors[x][z] = {0.5f, 0.5f, 0.5f}; // Rock
                }
            }
        }
    }

    float GetHeight(float wx, float wz) const {
        int x = (int)((wx / scale) + SIZE / 2);
        int z = (int)((wz / scale) + SIZE / 2);
        x = std::max(0, std::min(SIZE-1, x));
        z = std::max(0, std::min(SIZE-1, z));
        return heights[x][z];
    }

    void Draw() {
        glBegin(GL_TRIANGLES);
        for (int z = 0; z < SIZE - 1; z++) {
            for (int x = 0; x < SIZE - 1; x++) {
                float wx = (x - SIZE/2) * scale;
                float wz = (z - SIZE/2) * scale;

                // Triangle 1
                glColor3f(colors[x][z].r, colors[x][z].g, colors[x][z].b);
                glVertex3f(wx, heights[x][z], wz);
                glColor3f(colors[x][z+1].r, colors[x][z+1].g, colors[x][z+1].b);
                glVertex3f(wx, heights[x][z+1], wz + scale);
                glColor3f(colors[x+1][z].r, colors[x+1][z].g, colors[x+1][z].b);
                glVertex3f(wx + scale, heights[x+1][z], wz);

                // Triangle 2
                glColor3f(colors[x+1][z].r, colors[x+1][z].g, colors[x+1][z].b);
                glVertex3f(wx + scale, heights[x+1][z], wz);
                glColor3f(colors[x][z+1].r, colors[x][z+1].g, colors[x][z+1].b);
                glVertex3f(wx, heights[x][z+1], wz + scale);
                glColor3f(colors[x+1][z+1].r, colors[x+1][z+1].g, colors[x+1][z+1].b);
                glVertex3f(wx + scale, heights[x+1][z+1], wz + scale);
            }
        }
        glEnd();
    }

private:
    float Noise(float x, float y) {
        int n = (int)(x * 57 + y * 131);
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }
};

// ============================================================================
// Weapon System
// ============================================================================

enum class WeaponType { Pistol, Rifle, Shotgun, SMG, Sniper, RocketLauncher };

struct Weapon {
    std::string name;
    WeaponType type;
    float damage;
    float fireRate;
    int magSize;
    int ammo;
    int totalAmmo;
    float reload;
    float reloadTime;
    float spread;
    float range;
    int pellets = 1;
    Color color;

    bool CanFire() const { return reload <= 0 && ammo > 0; }

    void Fire() {
        if (CanFire()) {
            ammo--;
            reload = 1.0f / fireRate;
        }
    }

    void Reload() {
        if (ammo < magSize && totalAmmo > 0) {
            int needed = magSize - ammo;
            int take = std::min(needed, totalAmmo);
            ammo += take;
            totalAmmo -= take;
            reload = reloadTime;
        }
    }

    void Update(float dt) {
        if (reload > 0) reload -= dt;
    }

    void DrawHUD(float x, float y) {
        // Draw weapon info
        std::cout << "\r" << name << ": " << ammo << "/" << magSize
                  << " (" << totalAmmo << ")" << std::flush;
    }

    void DrawModel(const Vec3& pos, float yaw) {
        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);
        glRotatef(-yaw, 0, 1, 0);
        glTranslatef(0.3f, -0.2f, 0.5f);

        glColor3f(color.r, color.g, color.b);

        // Draw simple weapon shape based on type
        switch (type) {
            case WeaponType::Pistol:
                DrawBox(0, 0, 0, 0.04f, 0.08f, 0.12f);
                break;
            case WeaponType::Rifle:
                DrawBox(0, 0, 0, 0.03f, 0.05f, 0.4f);
                DrawBox(0, -0.05f, 0.1f, 0.02f, 0.06f, 0.03f);
                break;
            case WeaponType::Shotgun:
                DrawBox(0, 0, 0, 0.035f, 0.05f, 0.5f);
                break;
            case WeaponType::SMG:
                DrawBox(0, 0, 0, 0.03f, 0.04f, 0.25f);
                DrawBox(0, -0.04f, 0.05f, 0.015f, 0.06f, 0.02f);
                break;
            case WeaponType::Sniper:
                DrawBox(0, 0, 0, 0.025f, 0.04f, 0.6f);
                DrawBox(0, 0.04f, 0.15f, 0.02f, 0.025f, 0.1f);
                break;
            case WeaponType::RocketLauncher:
                DrawCylinder(0, 0, 0, 0.05f, 0.5f);
                break;
        }

        glPopMatrix();
    }

private:
    void DrawBox(float x, float y, float z, float sx, float sy, float sz) {
        glBegin(GL_QUADS);
        // Front
        glVertex3f(x-sx, y-sy, z+sz);
        glVertex3f(x+sx, y-sy, z+sz);
        glVertex3f(x+sx, y+sy, z+sz);
        glVertex3f(x-sx, y+sy, z+sz);
        // Back
        glVertex3f(x+sx, y-sy, z-sz);
        glVertex3f(x-sx, y-sy, z-sz);
        glVertex3f(x-sx, y+sy, z-sz);
        glVertex3f(x+sx, y+sy, z-sz);
        // Other faces...
        glVertex3f(x-sx, y-sy, z-sz);
        glVertex3f(x-sx, y-sy, z+sz);
        glVertex3f(x-sx, y+sy, z+sz);
        glVertex3f(x-sx, y+sy, z-sz);
        glVertex3f(x+sx, y-sy, z+sz);
        glVertex3f(x+sx, y-sy, z-sz);
        glVertex3f(x+sx, y+sy, z-sz);
        glVertex3f(x+sx, y+sy, z+sz);
        glVertex3f(x-sx, y+sy, z+sz);
        glVertex3f(x+sx, y+sy, z+sz);
        glVertex3f(x+sx, y+sy, z-sz);
        glVertex3f(x-sx, y+sy, z-sz);
        glVertex3f(x-sx, y-sy, z-sz);
        glVertex3f(x+sx, y-sy, z-sz);
        glVertex3f(x+sx, y-sy, z+sz);
        glVertex3f(x-sx, y-sy, z+sz);
        glEnd();
    }

    void DrawCylinder(float x, float y, float z, float r, float h) {
        int segs = 12;
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= segs; i++) {
            float a = i * 2.0f * 3.14159f / segs;
            float cx = cos(a) * r;
            float cy = sin(a) * r;
            glVertex3f(x + cx, y + cy, z);
            glVertex3f(x + cx, y + cy, z + h);
        }
        glEnd();
    }
};

// ============================================================================
// Enemy Soldier
// ============================================================================

struct Enemy {
    Vec3 pos;
    Vec3 vel;
    float health = 100;
    float yaw = 0;
    bool alive = true;
    float attackTimer = 0;
    float attackCooldown = 1.5f;
    int type = 0; // 0=infantry, 1=heavy

    void Update(const Vec3& playerPos, float terrainH, float dt) {
        if (!alive) return;

        // Face player
        Vec3 toPlayer = playerPos - pos;
        float dist = toPlayer.len();
        yaw = atan2(toPlayer.x, toPlayer.z) * 180.0f / 3.14159f;

        // Move toward player if not too close
        if (dist > 3.0f && dist < 50.0f) {
            Vec3 dir = toPlayer.norm();
            float speed = (type == 1) ? 3.0f : 5.0f;
            pos = pos + dir * speed * dt;
        }

        // Terrain following
        pos.y = terrainH + 0.9f;

        // Attack
        if (dist < 30.0f) {
            attackTimer -= dt;
        }
    }

    bool Hit(float damage) {
        health -= damage;
        if (health <= 0) {
            alive = false;
            return true;
        }
        return false;
    }

    bool CanAttack() {
        if (attackTimer <= 0 && alive) {
            attackTimer = attackCooldown;
            return true;
        }
        return false;
    }

    void Draw() {
        if (!alive) return;

        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);
        glRotatef(-yaw, 0, 1, 0);

        if (type == 1) {
            // Heavy enemy - darker, bigger
            glColor3f(0.3f, 0.25f, 0.2f);
            DrawSoldier(1.2f);
        } else {
            // Regular enemy
            glColor3f(0.4f, 0.35f, 0.3f);
            DrawSoldier(1.0f);
        }

        glPopMatrix();
    }

private:
    void DrawSoldier(float scale) {
        // Body
        glBegin(GL_QUADS);
        float w = 0.2f * scale, h = 0.5f * scale, d = 0.15f * scale;
        // Front
        glVertex3f(-w, 0, d); glVertex3f(w, 0, d); glVertex3f(w, h, d); glVertex3f(-w, h, d);
        // Back
        glVertex3f(w, 0, -d); glVertex3f(-w, 0, -d); glVertex3f(-w, h, -d); glVertex3f(w, h, -d);
        // Sides
        glVertex3f(-w, 0, -d); glVertex3f(-w, 0, d); glVertex3f(-w, h, d); glVertex3f(-w, h, -d);
        glVertex3f(w, 0, d); glVertex3f(w, 0, -d); glVertex3f(w, h, -d); glVertex3f(w, h, d);
        glEnd();

        // Head
        glColor3f(0.8f, 0.7f, 0.6f);
        glPushMatrix();
        glTranslatef(0, h + 0.12f * scale, 0);
        DrawSphere(0.1f * scale, 8);
        glPopMatrix();

        // Helmet
        glColor3f(0.3f, 0.35f, 0.25f);
        glPushMatrix();
        glTranslatef(0, h + 0.15f * scale, 0);
        DrawSphere(0.12f * scale, 8);
        glPopMatrix();
    }

    void DrawSphere(float r, int segs) {
        for (int lat = 0; lat < segs; lat++) {
            float t1 = (float)lat / segs * 3.14159f;
            float t2 = (float)(lat + 1) / segs * 3.14159f;
            glBegin(GL_QUAD_STRIP);
            for (int lon = 0; lon <= segs; lon++) {
                float p = (float)lon / segs * 2.0f * 3.14159f;
                glVertex3f(r * sin(t1) * cos(p), r * cos(t1), r * sin(t1) * sin(p));
                glVertex3f(r * sin(t2) * cos(p), r * cos(t2), r * sin(t2) * sin(p));
            }
            glEnd();
        }
    }
};

// ============================================================================
// Vehicle
// ============================================================================

struct Vehicle {
    enum Type { Tank, Jeep, Helicopter };
    Type type;
    Vec3 pos;
    float yaw = 0;
    float health = 500;
    bool alive = true;

    void Draw() {
        if (!alive) return;

        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);
        glRotatef(-yaw, 0, 1, 0);

        switch (type) {
            case Tank: DrawTank(); break;
            case Jeep: DrawJeep(); break;
            case Helicopter: DrawHelicopter(); break;
        }

        glPopMatrix();
    }

private:
    void DrawTank() {
        // Hull
        glColor3f(0.35f, 0.4f, 0.3f);
        DrawBox(0, 0.6f, 0, 1.25f, 0.4f, 2.0f);

        // Turret
        glColor3f(0.35f, 0.4f, 0.3f);
        DrawCylinder(0, 1.0f, 0, 0.6f, 0.4f);

        // Gun
        glColor3f(0.3f, 0.35f, 0.25f);
        DrawBox(0, 1.2f, 1.5f, 0.08f, 0.08f, 1.2f);

        // Tracks
        glColor3f(0.2f, 0.2f, 0.2f);
        DrawBox(-1.1f, 0.3f, 0, 0.25f, 0.3f, 2.1f);
        DrawBox(1.1f, 0.3f, 0, 0.25f, 0.3f, 2.1f);
    }

    void DrawJeep() {
        // Body
        glColor3f(0.35f, 0.4f, 0.3f);
        DrawBox(0, 0.5f, 0, 0.9f, 0.3f, 1.75f);

        // Cabin
        glColor3f(0.3f, 0.35f, 0.25f);
        DrawBox(0, 0.9f, -0.4f, 0.8f, 0.4f, 0.75f);

        // Wheels
        glColor3f(0.15f, 0.15f, 0.15f);
        DrawCylinder(-0.8f, 0.3f, 1.0f, 0.3f, 0.15f);
        DrawCylinder(0.8f, 0.3f, 1.0f, 0.3f, 0.15f);
        DrawCylinder(-0.8f, 0.3f, -0.8f, 0.3f, 0.15f);
        DrawCylinder(0.8f, 0.3f, -0.8f, 0.3f, 0.15f);
    }

    void DrawHelicopter() {
        // Fuselage
        glColor3f(0.35f, 0.4f, 0.3f);
        DrawBox(0, 0, 0, 0.6f, 0.5f, 1.5f);

        // Tail
        glColor3f(0.35f, 0.4f, 0.3f);
        DrawBox(0, 0.2f, -2.0f, 0.15f, 0.2f, 1.0f);

        // Main rotor
        glColor3f(0.3f, 0.3f, 0.3f);
        DrawBox(0, 0.8f, 0, 2.5f, 0.02f, 0.1f);
        DrawBox(0, 0.8f, 0, 0.1f, 0.02f, 2.5f);

        // Skids
        glColor3f(0.2f, 0.2f, 0.2f);
        DrawBox(-0.5f, -0.6f, 0, 0.03f, 0.03f, 1.2f);
        DrawBox(0.5f, -0.6f, 0, 0.03f, 0.03f, 1.2f);
    }

    void DrawBox(float x, float y, float z, float sx, float sy, float sz) {
        glBegin(GL_QUADS);
        glVertex3f(x-sx, y-sy, z+sz); glVertex3f(x+sx, y-sy, z+sz);
        glVertex3f(x+sx, y+sy, z+sz); glVertex3f(x-sx, y+sy, z+sz);
        glVertex3f(x+sx, y-sy, z-sz); glVertex3f(x-sx, y-sy, z-sz);
        glVertex3f(x-sx, y+sy, z-sz); glVertex3f(x+sx, y+sy, z-sz);
        glVertex3f(x-sx, y-sy, z-sz); glVertex3f(x-sx, y-sy, z+sz);
        glVertex3f(x-sx, y+sy, z+sz); glVertex3f(x-sx, y+sy, z-sz);
        glVertex3f(x+sx, y-sy, z+sz); glVertex3f(x+sx, y-sy, z-sz);
        glVertex3f(x+sx, y+sy, z-sz); glVertex3f(x+sx, y+sy, z+sz);
        glVertex3f(x-sx, y+sy, z+sz); glVertex3f(x+sx, y+sy, z+sz);
        glVertex3f(x+sx, y+sy, z-sz); glVertex3f(x-sx, y+sy, z-sz);
        glVertex3f(x-sx, y-sy, z-sz); glVertex3f(x+sx, y-sy, z-sz);
        glVertex3f(x+sx, y-sy, z+sz); glVertex3f(x-sx, y-sy, z+sz);
        glEnd();
    }

    void DrawCylinder(float x, float y, float z, float r, float h) {
        int segs = 12;
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= segs; i++) {
            float a = i * 2.0f * 3.14159f / segs;
            float cx = cos(a) * r;
            float cz = sin(a) * r;
            glVertex3f(x + cx, y - h, z + cz);
            glVertex3f(x + cx, y + h, z + cz);
        }
        glEnd();
    }
};

// ============================================================================
// Building
// ============================================================================

struct Building {
    Vec3 pos;
    float width, depth, height;
    Color color;
    bool destroyed = false;

    void Draw() {
        if (destroyed) return;

        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);

        glColor3f(color.r, color.g, color.b);

        // Main structure
        glBegin(GL_QUADS);
        float w = width / 2, h = height, d = depth / 2;
        // Front
        glVertex3f(-w, 0, d); glVertex3f(w, 0, d);
        glVertex3f(w, h, d); glVertex3f(-w, h, d);
        // Back
        glVertex3f(w, 0, -d); glVertex3f(-w, 0, -d);
        glVertex3f(-w, h, -d); glVertex3f(w, h, -d);
        // Sides
        glVertex3f(-w, 0, -d); glVertex3f(-w, 0, d);
        glVertex3f(-w, h, d); glVertex3f(-w, h, -d);
        glVertex3f(w, 0, d); glVertex3f(w, 0, -d);
        glVertex3f(w, h, -d); glVertex3f(w, h, d);
        // Top
        glVertex3f(-w, h, d); glVertex3f(w, h, d);
        glVertex3f(w, h, -d); glVertex3f(-w, h, -d);
        glEnd();

        // Windows
        glColor3f(0.3f, 0.4f, 0.5f);
        float windowSize = 0.8f;
        for (float y = 2.0f; y < height - 1; y += 3.0f) {
            for (float x = -width/3; x <= width/3; x += 2.5f) {
                glBegin(GL_QUADS);
                glVertex3f(x - windowSize/2, y, d + 0.01f);
                glVertex3f(x + windowSize/2, y, d + 0.01f);
                glVertex3f(x + windowSize/2, y + windowSize*1.5f, d + 0.01f);
                glVertex3f(x - windowSize/2, y + windowSize*1.5f, d + 0.01f);
                glEnd();
            }
        }

        glPopMatrix();
    }
};

// ============================================================================
// Player
// ============================================================================

struct Player {
    Vec3 pos = Vec3(0, 2, 0);
    Vec3 vel;
    float yaw = 0, pitch = 0;
    float health = 100;
    float armor = 50;
    int score = 0;
    int kills = 0;
    int currentWeapon = 0;
    std::vector<Weapon> weapons;
    bool sprinting = false;

    Player() {
        // Setup weapons
        weapons.push_back({"M1911 Pistol", WeaponType::Pistol, 35, 3, 7, 7, 56, 0, 1.5f, 0.02f, 50, 1, {0.25f, 0.25f, 0.25f}});
        weapons.push_back({"M4 Rifle", WeaponType::Rifle, 30, 10, 30, 30, 180, 0, 2.5f, 0.01f, 100, 1, {0.2f, 0.2f, 0.2f}});
        weapons.push_back({"Shotgun", WeaponType::Shotgun, 15, 1.5f, 8, 8, 32, 0, 3.0f, 0.08f, 20, 8, {0.3f, 0.25f, 0.2f}});
        weapons.push_back({"MP5 SMG", WeaponType::SMG, 20, 15, 25, 25, 150, 0, 2.0f, 0.03f, 40, 1, {0.22f, 0.22f, 0.22f}});
        weapons.push_back({"Sniper Rifle", WeaponType::Sniper, 100, 0.8f, 5, 5, 25, 0, 3.5f, 0.001f, 200, 1, {0.18f, 0.18f, 0.18f}});
        weapons.push_back({"RPG", WeaponType::RocketLauncher, 300, 0.5f, 1, 1, 5, 0, 4.0f, 0.0f, 150, 1, {0.35f, 0.4f, 0.3f}});
    }

    Vec3 GetLookDir() const {
        float ry = yaw * 3.14159f / 180;
        float rp = pitch * 3.14159f / 180;
        return Vec3(sin(ry)*cos(rp), sin(rp), cos(ry)*cos(rp)).norm();
    }

    void Update(float dt) {
        for (auto& w : weapons) w.Update(dt);
    }

    Weapon& CurrentWeapon() { return weapons[currentWeapon]; }

    void TakeDamage(float dmg) {
        float absorbed = std::min(dmg * 0.5f, armor);
        armor -= absorbed;
        health -= (dmg - absorbed);
        if (armor < 0) armor = 0;
    }
};

// ============================================================================
// Game State
// ============================================================================

struct GameState {
    Player player;
    Terrain terrain;
    std::vector<Enemy> enemies;
    std::vector<Vehicle> vehicles;
    std::vector<Building> buildings;
    int wave = 1;
    int enemiesPerWave = 10;
    float spawnTimer = 0;
    int enemiesToSpawn = 0;
    bool gameOver = false;

    void Initialize() {
        terrain.Generate(12345);

        // Spawn initial buildings
        buildings.push_back({Vec3(-30, terrain.GetHeight(-30, 20), 20), 8, 10, 12, {0.7f, 0.65f, 0.6f}});
        buildings.push_back({Vec3(25, terrain.GetHeight(25, -15), -15), 12, 8, 8, {0.6f, 0.55f, 0.5f}});
        buildings.push_back({Vec3(40, terrain.GetHeight(40, 30), 30), 6, 6, 15, {0.65f, 0.6f, 0.55f}});
        buildings.push_back({Vec3(-45, terrain.GetHeight(-45, -35), -35), 15, 20, 6, {0.55f, 0.5f, 0.45f}});

        // Spawn vehicles
        vehicles.push_back({Vehicle::Tank, Vec3(20, terrain.GetHeight(20, 0) + 0.5f, 0), 45, 500, true});
        vehicles.push_back({Vehicle::Jeep, Vec3(-15, terrain.GetHeight(-15, 25) + 0.5f, 25), -30, 200, true});
        vehicles.push_back({Vehicle::Helicopter, Vec3(0, 15, -40), 0, 400, true});

        StartWave();
    }

    void StartWave() {
        enemiesToSpawn = enemiesPerWave + wave * 3;
        spawnTimer = 0;
    }

    void Update(float dt) {
        if (gameOver) return;

        // Spawn enemies
        if (enemiesToSpawn > 0) {
            spawnTimer -= dt;
            if (spawnTimer <= 0) {
                SpawnEnemy();
                enemiesToSpawn--;
                spawnTimer = 2.0f / wave;
            }
        }

        // Update enemies
        for (auto& e : enemies) {
            float th = terrain.GetHeight(e.pos.x, e.pos.z);
            e.Update(player.pos, th, dt);

            if (e.CanAttack()) {
                float dist = (player.pos - e.pos).len();
                if (dist < 25.0f) {
                    float dmg = (e.type == 1) ? 15.0f : 8.0f;
                    player.TakeDamage(dmg);
                }
            }
        }

        // Remove dead enemies
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
            [](const Enemy& e) { return !e.alive; }), enemies.end());

        // Check wave complete
        if (enemies.empty() && enemiesToSpawn == 0) {
            wave++;
            StartWave();
        }

        // Check game over
        if (player.health <= 0) {
            gameOver = true;
        }

        player.Update(dt);
    }

    void SpawnEnemy() {
        Enemy e;
        float angle = (rand() % 360) * 3.14159f / 180;
        float dist = 40 + rand() % 20;
        e.pos = Vec3(player.pos.x + cos(angle) * dist, 0, player.pos.z + sin(angle) * dist);
        e.pos.y = terrain.GetHeight(e.pos.x, e.pos.z) + 0.9f;
        e.type = (rand() % 4 == 0) ? 1 : 0; // 25% heavy enemies
        e.health = (e.type == 1) ? 200 : 100;
        enemies.push_back(e);
    }

    bool Shoot() {
        Weapon& w = player.CurrentWeapon();
        if (!w.CanFire()) return false;

        w.Fire();
        Vec3 look = player.GetLookDir();

        bool hit = false;
        for (int p = 0; p < w.pellets; p++) {
            Vec3 dir = look;
            if (w.spread > 0) {
                dir.x += ((rand() % 1000) / 1000.0f - 0.5f) * w.spread;
                dir.y += ((rand() % 1000) / 1000.0f - 0.5f) * w.spread;
                dir.z += ((rand() % 1000) / 1000.0f - 0.5f) * w.spread;
                dir = dir.norm();
            }

            float closestDist = w.range;
            Enemy* closestEnemy = nullptr;

            for (auto& e : enemies) {
                if (!e.alive) continue;
                Vec3 toEnemy = e.pos - player.pos;
                float dist = toEnemy.len();
                if (dist > w.range) continue;

                float angle = acos(toEnemy.norm().dot(dir));
                if (angle < 0.15f && dist < closestDist) {
                    closestDist = dist;
                    closestEnemy = &e;
                }
            }

            if (closestEnemy) {
                if (closestEnemy->Hit(w.damage / w.pellets)) {
                    player.score += (closestEnemy->type == 1) ? 200 : 100;
                    player.kills++;
                }
                hit = true;
            }
        }

        return hit;
    }
};

// ============================================================================
// Input State
// ============================================================================

struct Input {
    bool w, a, s, d, space, shift, ctrl;
    bool reload;
    float mx, my;
    bool fire;
    int weaponSwitch;
} input = {};

void KeyCallback(GLFWwindow*, int k, int, int a, int) {
    bool p = (a == GLFW_PRESS || a == GLFW_REPEAT);
    if (k == GLFW_KEY_W) input.w = p;
    if (k == GLFW_KEY_A) input.a = p;
    if (k == GLFW_KEY_S) input.s = p;
    if (k == GLFW_KEY_D) input.d = p;
    if (k == GLFW_KEY_SPACE) input.space = p;
    if (k == GLFW_KEY_LEFT_SHIFT) input.shift = p;
    if (k == GLFW_KEY_LEFT_CONTROL) input.ctrl = p;
    if (k == GLFW_KEY_R && a == GLFW_PRESS) input.reload = true;

    if (k >= GLFW_KEY_1 && k <= GLFW_KEY_6 && a == GLFW_PRESS) {
        input.weaponSwitch = k - GLFW_KEY_1;
    }
}

void MouseCallback(GLFWwindow*, int b, int a, int) {
    if (b == GLFW_MOUSE_BUTTON_LEFT) input.fire = (a == GLFW_PRESS);
}

void CursorCallback(GLFWwindow*, double x, double y) {
    static double lx = x, ly = y;
    input.mx = (float)(x - lx) * 0.1f;
    input.my = (float)(ly - y) * 0.1f;
    lx = x; ly = y;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "===========================================\n";
    std::cout << "  DRX ENGINE - BATTLEFIELD DEMO\n";
    std::cout << "===========================================\n";
    std::cout << "Controls:\n";
    std::cout << "  WASD - Move, Shift - Sprint, Space - Jump\n";
    std::cout << "  Mouse - Look, Left Click - Fire\n";
    std::cout << "  1-6 - Switch weapons, R - Reload\n";
    std::cout << "  ESC - Quit\n";
    std::cout << "===========================================\n\n";

    srand((unsigned)time(0));

    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "DRX Battlefield Demo", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseCallback);
    glfwSetCursorPosCallback(window, CursorCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSwapInterval(1);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.5f, 0.7f, 0.9f, 1.0f);

    // Projection matrix
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float fov = 70.0f * 3.14159f / 180.0f;
    float aspect = 1280.0f / 720.0f;
    float f = 1.0f / tan(fov * 0.5f);
    float nearZ = 0.1f, farZ = 500.0f;
    float proj[16] = {
        f/aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (farZ+nearZ)/(nearZ-farZ), -1,
        0, 0, (2*farZ*nearZ)/(nearZ-farZ), 0
    };
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);

    GameState game;
    game.Initialize();

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        if (dt > 0.033f) dt = 0.033f;

        // Player movement
        Player& p = game.player;
        p.yaw += input.mx;
        p.pitch += input.my;
        input.mx = 0; input.my = 0;
        if (p.pitch > 89) p.pitch = 89;
        if (p.pitch < -89) p.pitch = -89;

        float ry = p.yaw * 3.14159f / 180;
        Vec3 forward(sin(ry), 0, cos(ry));
        Vec3 right(cos(ry), 0, -sin(ry));
        Vec3 move;

        if (input.w) move = move + forward;
        if (input.s) move = move - forward;
        if (input.a) move = move - right;
        if (input.d) move = move + right;

        float speed = input.shift ? 12.0f : 6.0f;
        if (move.len() > 0) {
            p.pos = p.pos + move.norm() * speed * dt;
        }

        // Terrain collision
        float th = game.terrain.GetHeight(p.pos.x, p.pos.z);
        p.pos.y = th + 1.7f;

        // Weapon switch
        if (input.weaponSwitch >= 0 && input.weaponSwitch < (int)p.weapons.size()) {
            p.currentWeapon = input.weaponSwitch;
            input.weaponSwitch = -1;
        }

        // Reload
        if (input.reload) {
            p.CurrentWeapon().Reload();
            input.reload = false;
        }

        // Fire
        if (input.fire && !game.gameOver) {
            game.Shoot();
        }

        // Update game
        game.Update(dt);

        // Render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();

        // Camera
        Vec3 look = p.GetLookDir();
        Vec3 eye = p.pos;
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

        // Draw scene
        game.terrain.Draw();

        for (auto& b : game.buildings) b.Draw();
        for (auto& v : game.vehicles) v.Draw();
        for (auto& e : game.enemies) e.Draw();

        // Draw weapon model
        p.CurrentWeapon().DrawModel(p.pos, p.yaw);

        // Draw crosshair
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, 1280, 720, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);

        // Crosshair
        glColor3f(1, 1, 1);
        glBegin(GL_LINES);
        glVertex2f(635, 360); glVertex2f(645, 360);
        glVertex2f(640, 355); glVertex2f(640, 365);
        glEnd();

        // HUD
        glColor3f(1, 1, 1);
        // Health bar
        glBegin(GL_QUADS);
        glColor3f(0.3f, 0.3f, 0.3f);
        glVertex2f(20, 680); glVertex2f(220, 680);
        glVertex2f(220, 700); glVertex2f(20, 700);
        glColor3f(0.8f, 0.2f, 0.2f);
        glVertex2f(20, 680); glVertex2f(20 + p.health * 2, 680);
        glVertex2f(20 + p.health * 2, 700); glVertex2f(20, 700);
        glEnd();

        // Armor bar
        glBegin(GL_QUADS);
        glColor3f(0.3f, 0.3f, 0.3f);
        glVertex2f(20, 655); glVertex2f(120, 655);
        glVertex2f(120, 675); glVertex2f(20, 675);
        glColor3f(0.2f, 0.5f, 0.8f);
        glVertex2f(20, 655); glVertex2f(20 + p.armor, 655);
        glVertex2f(20 + p.armor, 675); glVertex2f(20, 675);
        glEnd();

        glEnable(GL_DEPTH_TEST);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        // Console output
        static int frame = 0;
        if (++frame % 15 == 0) {
            Weapon& w = p.CurrentWeapon();
            std::cout << "\rHP:" << (int)p.health << " ARM:" << (int)p.armor
                      << " | " << w.name << " " << w.ammo << "/" << w.magSize
                      << " [" << w.totalAmmo << "]"
                      << " | Score:" << p.score << " Kills:" << p.kills
                      << " Wave:" << game.wave
                      << " Enemies:" << game.enemies.size()
                      << (game.gameOver ? " GAME OVER" : "")
                      << "          " << std::flush;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE)) break;
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "\n\n===========================================\n";
    std::cout << "FINAL SCORE: " << game.player.score << "\n";
    std::cout << "TOTAL KILLS: " << game.player.kills << "\n";
    std::cout << "WAVES SURVIVED: " << game.wave << "\n";
    std::cout << "===========================================\n";

    return 0;
}
