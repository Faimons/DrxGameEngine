/**
 * DRX Engine - Procedural Mesh Generation
 * Generate 3D meshes programmatically
 */

#pragma once

#include <vector>
#include <cmath>
#include <string>

namespace DRX::Procedural {

// ============================================================================
// Basic Types
// ============================================================================

struct Vertex {
    float x, y, z;      // Position
    float nx, ny, nz;   // Normal
    float u, v;         // UV
    float r, g, b, a;   // Color

    Vertex() : x(0), y(0), z(0), nx(0), ny(1), nz(0), u(0), v(0), r(1), g(1), b(1), a(1) {}
    Vertex(float px, float py, float pz) : x(px), y(py), z(pz), nx(0), ny(1), nz(0), u(0), v(0), r(1), g(1), b(1), a(1) {}
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::string name;

    void Clear() {
        vertices.clear();
        indices.clear();
    }

    void CalculateNormals();
    void Transform(float tx, float ty, float tz);
    void Scale(float sx, float sy, float sz);
    void Rotate(float rx, float ry, float rz);
    void SetColor(float r, float g, float b, float a = 1.0f);
    void Merge(const Mesh& other);
};

// ============================================================================
// Primitive Generator
// ============================================================================

class PrimitiveGenerator {
public:
    static Mesh CreateCube(float size = 1.0f);
    static Mesh CreateSphere(float radius = 1.0f, int segments = 16);
    static Mesh CreateCylinder(float radius = 0.5f, float height = 1.0f, int segments = 16);
    static Mesh CreateCone(float radius = 0.5f, float height = 1.0f, int segments = 16);
    static Mesh CreatePlane(float width = 1.0f, float depth = 1.0f, int subdivisions = 1);
    static Mesh CreateTorus(float majorRadius = 1.0f, float minorRadius = 0.3f, int majorSegs = 24, int minorSegs = 12);
    static Mesh CreateCapsule(float radius = 0.5f, float height = 2.0f, int segments = 16);
};

// ============================================================================
// Terrain Generator
// ============================================================================

class TerrainGenerator {
public:
    struct TerrainSettings {
        int width = 64;
        int depth = 64;
        float cellSize = 1.0f;
        float maxHeight = 10.0f;
        int octaves = 4;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
        unsigned int seed = 12345;
    };

    static Mesh Generate(const TerrainSettings& settings);
    static Mesh GenerateFlat(int width, int depth, float cellSize);
    static Mesh GenerateIsland(const TerrainSettings& settings);
    static Mesh GenerateMountain(const TerrainSettings& settings);
    static Mesh GenerateValley(const TerrainSettings& settings);

private:
    static float Noise2D(float x, float y, unsigned int seed);
    static float PerlinNoise(float x, float y, unsigned int seed, int octaves, float persistence, float lacunarity);
};

// ============================================================================
// Weapon Factory
// ============================================================================

class WeaponFactory {
public:
    enum class WeaponType {
        Pistol,
        Rifle,
        Shotgun,
        SMG,
        Sniper,
        RocketLauncher,
        Knife
    };

    struct WeaponSettings {
        WeaponType type = WeaponType::Rifle;
        float scale = 1.0f;
        float r = 0.3f, g = 0.3f, b = 0.3f; // Color
    };

    static Mesh CreateWeapon(const WeaponSettings& settings);
    static Mesh CreatePistol(float scale = 1.0f);
    static Mesh CreateRifle(float scale = 1.0f);
    static Mesh CreateShotgun(float scale = 1.0f);
    static Mesh CreateSMG(float scale = 1.0f);
    static Mesh CreateSniper(float scale = 1.0f);
    static Mesh CreateRocketLauncher(float scale = 1.0f);
    static Mesh CreateKnife(float scale = 1.0f);
};

// ============================================================================
// Character Factory
// ============================================================================

class CharacterFactory {
public:
    struct CharacterSettings {
        float height = 1.8f;
        float shoulderWidth = 0.45f;
        float hipWidth = 0.35f;
        float headSize = 0.25f;
        float armLength = 0.7f;
        float legLength = 0.9f;
        bool includeWeapon = false;
    };

    struct CharacterMeshes {
        Mesh body;
        Mesh head;
        Mesh leftArm;
        Mesh rightArm;
        Mesh leftLeg;
        Mesh rightLeg;
        Mesh combined;
    };

    static CharacterMeshes CreateCharacter(const CharacterSettings& settings);
    static Mesh CreateSoldier(float height = 1.8f);
    static Mesh CreateRobot(float height = 2.0f);
};

// ============================================================================
// Building Factory
// ============================================================================

class BuildingFactory {
public:
    struct BuildingSettings {
        float width = 10.0f;
        float depth = 10.0f;
        float height = 8.0f;
        int floors = 2;
        bool hasRoof = true;
        bool hasWindows = true;
        bool hasDoor = true;
    };

    static Mesh CreateBuilding(const BuildingSettings& settings);
    static Mesh CreateHouse(float width = 8.0f, float depth = 10.0f);
    static Mesh CreateWarehouse(float width = 20.0f, float depth = 30.0f);
    static Mesh CreateTower(float radius = 3.0f, float height = 15.0f);
    static Mesh CreateBunker(float width = 5.0f, float depth = 5.0f);
    static Mesh CreateWall(float length = 10.0f, float height = 3.0f, float thickness = 0.5f);
};

// ============================================================================
// Vehicle Factory
// ============================================================================

class VehicleFactory {
public:
    static Mesh CreateTank(float scale = 1.0f);
    static Mesh CreateJeep(float scale = 1.0f);
    static Mesh CreateHelicopter(float scale = 1.0f);
    static Mesh CreatePlane(float scale = 1.0f);
    static Mesh CreateBoat(float scale = 1.0f);
};

// ============================================================================
// Environment Objects
// ============================================================================

class EnvironmentFactory {
public:
    static Mesh CreateTree(float height = 5.0f);
    static Mesh CreateRock(float size = 1.0f);
    static Mesh CreateBarrel(float radius = 0.5f, float height = 1.0f);
    static Mesh CreateCrate(float size = 1.0f);
    static Mesh CreateSandbags(float width = 2.0f);
    static Mesh CreateFence(float length = 5.0f, float height = 1.5f);
};

} // namespace DRX::Procedural
