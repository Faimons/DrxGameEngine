/**
 * DRX Engine - FPS Demo
 * A sample game demonstrating the engine's FPS framework
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Engine.h"
#include "Renderer/Public/DeferredRenderer.h"
#include "Game/Public/FPSFramework.h"
#include "Camera/Public/CameraSystem.h"
#include "Terrain/Public/TerrainSystem.h"
#include "Input/Public/InputSystem.h"
#include "Audio/Public/AudioSystem.h"
#include "Debug/Public/DebugSystem.h"

using namespace DRX;

/**
 * Demo Game Mode - Wave-based survival
 */
class SurvivalGameMode : public Game::GameMode {
public:
    void Initialize() override {
        GameMode::Initialize();
        wave_number_ = 0;
        enemies_remaining_ = 0;
        spawn_timer_ = 0;
    }

    void Update(float32 delta_time) override {
        GameMode::Update(delta_time);

        if (!IsMatchActive()) return;

        // Check if wave is complete
        if (enemies_remaining_ <= 0) {
            StartNextWave();
        }

        // Spawn enemies
        if (enemies_to_spawn_ > 0) {
            spawn_timer_ -= delta_time;
            if (spawn_timer_ <= 0) {
                SpawnEnemy();
                spawn_timer_ = spawn_interval_;
            }
        }
    }

    void OnEnemyDeath(Game::Enemy* enemy, const Game::DamageInfo& damage) override {
        GameMode::OnEnemyDeath(enemy, damage);
        enemies_remaining_--;
    }

    int32 GetWaveNumber() const { return wave_number_; }
    int32 GetEnemiesRemaining() const { return enemies_remaining_; }

private:
    void StartNextWave() {
        wave_number_++;
        enemies_to_spawn_ = 5 + wave_number_ * 2;  // Increasing difficulty
        enemies_remaining_ = enemies_to_spawn_;
        spawn_interval_ = std::max(0.5f, 2.0f - wave_number_ * 0.1f);
        spawn_timer_ = 2.0f;  // Delay before first spawn
    }

    void SpawnEnemy() {
        if (enemies_to_spawn_ <= 0) return;

        Game::EnemyConfig config;
        config.name = "Zombie";
        config.max_health = 50.0f + wave_number_ * 10.0f;
        config.damage = 10.0f + wave_number_ * 2.0f;
        config.walk_speed = 2.0f + wave_number_ * 0.1f;
        config.run_speed = 4.0f + wave_number_ * 0.2f;
        config.attack_range = 2.0f;
        config.sight_range = 30.0f;
        config.score_value = 100 + wave_number_ * 10;

        auto spawn = Game::GameWorld::Instance().GetRandomSpawnPoint(false);
        Game::GameWorld::Instance().SpawnEnemy(config, spawn.position);

        enemies_to_spawn_--;
    }

    int32 wave_number_ = 0;
    int32 enemies_remaining_ = 0;
    int32 enemies_to_spawn_ = 0;
    float32 spawn_timer_ = 0;
    float32 spawn_interval_ = 1.0f;
};

/**
 * FPS Demo Application
 */
class FPSDemoApp {
public:
    bool Initialize() {
        // Initialize core systems
        Debug::Console::Instance().Initialize();
        Debug::Profiler::Instance().SetEnabled(true);

        // Create window and renderer
        // Renderer::DeferredRenderer::Instance().Initialize(1920, 1080);

        // Initialize audio
        // Audio::AudioSystem::Instance().Initialize();

        // Initialize input
        InitializeInput();

        // Initialize terrain
        InitializeTerrain();

        // Initialize game
        InitializeGame();

        Debug::Console::Instance().Print("FPS Demo initialized");
        return true;
    }

    void Shutdown() {
        Game::GameWorld::Instance().Shutdown();
        Terrain::TerrainManager::Instance().Shutdown();
        Camera::CameraManager::Instance().Shutdown();
        Debug::Console::Instance().Shutdown();
    }

    void Run() {
        running_ = true;
        float32 delta_time = 1.0f / 60.0f;
        const float32 fixed_delta = 1.0f / 60.0f;
        float32 accumulator = 0;

        while (running_) {
            DRX_PROFILE_SCOPE("Frame");

            Debug::Profiler::Instance().BeginFrame();

            // Process input
            ProcessInput();

            // Fixed timestep physics
            accumulator += delta_time;
            while (accumulator >= fixed_delta) {
                {
                    DRX_PROFILE_SCOPE("FixedUpdate");
                    Game::GameWorld::Instance().FixedUpdate(fixed_delta);
                }
                accumulator -= fixed_delta;
            }

            // Update game
            {
                DRX_PROFILE_SCOPE("Update");
                Update(delta_time);
            }

            // Render
            {
                DRX_PROFILE_SCOPE("Render");
                Render();
            }

            Debug::Profiler::Instance().EndFrame();

            // Simple frame timing
            // In real app, would use proper timing
        }
    }

private:
    void InitializeInput() {
        auto& input = Input::InputSystem::Instance();
        // input.Initialize(window_handle);

        // Bind actions
        input.CreateAction("Move");
        input.CreateAction("Look");
        input.CreateAction("Fire");
        input.CreateAction("Aim");
        input.CreateAction("Reload");
        input.CreateAction("Jump");
        input.CreateAction("Sprint");
        input.CreateAction("Crouch");

        // Keyboard bindings
        input.BindKey(Input::KeyCode::W, "MoveForward");
        input.BindKey(Input::KeyCode::S, "MoveBack");
        input.BindKey(Input::KeyCode::A, "MoveLeft");
        input.BindKey(Input::KeyCode::D, "MoveRight");
        input.BindKey(Input::KeyCode::Space, "Jump");
        input.BindKey(Input::KeyCode::LeftShift, "Sprint");
        input.BindKey(Input::KeyCode::LeftControl, "Crouch");
        input.BindKey(Input::KeyCode::R, "Reload");

        // Mouse bindings
        input.BindMouseButton(Input::MouseButton::Left, "Fire");
        input.BindMouseButton(Input::MouseButton::Right, "Aim");
    }

    void InitializeTerrain() {
        Terrain::TerrainConfig config;
        config.terrain_width = 256.0f;
        config.terrain_length = 256.0f;
        config.height_scale = 20.0f;
        config.num_chunks_x = 4;
        config.num_chunks_z = 4;
        config.chunk_resolution = 32;

        auto& terrain_mgr = Terrain::TerrainManager::Instance();
        terrain_mgr.Initialize();

        terrain_ = terrain_mgr.CreateTerrain("MainTerrain", config);
        terrain_mgr.SetActiveTerrain(terrain_);

        // Generate procedural terrain
        terrain_->GetHeightmap().GeneratePerlinNoise(4.0f, 6, 0.5f);
        terrain_->GetHeightmap().ApplyHydraulicErosion(10000);

        // Add terrain layers
        Terrain::TerrainLayer grass;
        grass.name = "Grass";
        grass.tiling_x = 20.0f;
        grass.tiling_y = 20.0f;
        grass.roughness = 0.9f;
        terrain_->AddLayer(grass);

        Terrain::TerrainLayer rock;
        rock.name = "Rock";
        rock.tiling_x = 10.0f;
        rock.tiling_y = 10.0f;
        rock.roughness = 0.7f;
        terrain_->AddLayer(rock);

        // Add foliage
        Terrain::FoliageType tree;
        tree.name = "Tree";
        tree.density = 0.5f;
        tree.min_scale = 0.8f;
        tree.max_scale = 1.5f;
        tree.min_slope = 0.0f;
        tree.max_slope = 30.0f;
        terrain_->GetFoliage().AddFoliageType(tree);

        terrain_->GetFoliage().Generate(
            terrain_->GetHeightmap(),
            terrain_->GetSplatMap(),
            config
        );
    }

    void InitializeGame() {
        auto& world = Game::GameWorld::Instance();
        world.Initialize();

        // Create game mode
        auto game_mode = std::make_shared<SurvivalGameMode>();
        world.SetGameMode(game_mode);
        survival_mode_ = static_cast<SurvivalGameMode*>(game_mode.get());

        // Add spawn points
        for (int i = 0; i < 4; ++i) {
            Game::SpawnPoint player_spawn;
            player_spawn.position = Math::Vector3(128.0f + i * 10.0f, 0, 128.0f);
            player_spawn.is_player_spawn = true;
            world.AddSpawnPoint(player_spawn);
        }

        for (int i = 0; i < 8; ++i) {
            Game::SpawnPoint enemy_spawn;
            float angle = i * 45.0f * 3.14159265359f / 180.0f;
            enemy_spawn.position = Math::Vector3(
                128.0f + cos(angle) * 50.0f,
                0,
                128.0f + sin(angle) * 50.0f
            );
            enemy_spawn.is_enemy_spawn = true;
            world.AddSpawnPoint(enemy_spawn);
        }

        // Spawn player
        auto spawn = world.GetRandomSpawnPoint(true);
        player_ = world.SpawnPlayer(spawn.position);

        // Give player weapons
        GivePlayerWeapons();

        // Initialize camera
        Camera::CameraManager::Instance().Initialize();
        camera_ = Camera::CameraManager::Instance().GetMainCamera();
        camera_->SetFieldOfView(90.0f);
        camera_->SetNearPlane(0.1f);
        camera_->SetFarPlane(500.0f);

        // Start match
        game_mode->StartMatch();
    }

    void GivePlayerWeapons() {
        // Assault Rifle
        Game::WeaponConfig rifle_config;
        rifle_config.name = "Assault Rifle";
        rifle_config.slot = Game::WeaponSlot::Primary;
        rifle_config.damage = 25.0f;
        rifle_config.fire_rate = 10.0f;
        rifle_config.magazine_size = 30;
        rifle_config.max_ammo = 120;
        rifle_config.reload_time = 2.0f;
        rifle_config.spread = 2.0f;
        rifle_config.recoil = 3.0f;
        rifle_config.range = 100.0f;
        rifle_config.is_hitscan = true;
        rifle_config.aim_fov = 60.0f;

        auto rifle = std::make_shared<Game::Weapon>(rifle_config);
        player_->GiveWeapon(rifle);

        // Pistol
        Game::WeaponConfig pistol_config;
        pistol_config.name = "Pistol";
        pistol_config.slot = Game::WeaponSlot::Secondary;
        pistol_config.damage = 35.0f;
        pistol_config.fire_rate = 4.0f;
        pistol_config.magazine_size = 12;
        pistol_config.max_ammo = 48;
        pistol_config.reload_time = 1.5f;
        pistol_config.spread = 1.0f;
        pistol_config.recoil = 5.0f;
        pistol_config.range = 50.0f;
        pistol_config.is_hitscan = true;
        pistol_config.aim_fov = 70.0f;

        auto pistol = std::make_shared<Game::Weapon>(pistol_config);
        player_->GiveWeapon(pistol);

        // Knife
        Game::WeaponConfig knife_config;
        knife_config.name = "Knife";
        knife_config.slot = Game::WeaponSlot::Melee;
        knife_config.damage = 50.0f;
        knife_config.fire_rate = 2.0f;
        knife_config.magazine_size = 1;
        knife_config.max_ammo = 999;
        knife_config.reload_time = 0.0f;
        knife_config.range = 2.0f;
        knife_config.is_hitscan = true;

        auto knife = std::make_shared<Game::Weapon>(knife_config);
        player_->GiveWeapon(knife);
    }

    void ProcessInput() {
        auto& input = Input::InputSystem::Instance();
        // input.Update();

        if (!player_) return;

        // Movement
        Math::Vector2 move_input;
        if (input.IsKeyDown(Input::KeyCode::W)) move_input.y += 1.0f;
        if (input.IsKeyDown(Input::KeyCode::S)) move_input.y -= 1.0f;
        if (input.IsKeyDown(Input::KeyCode::A)) move_input.x -= 1.0f;
        if (input.IsKeyDown(Input::KeyCode::D)) move_input.x += 1.0f;
        player_->SetMoveInput(move_input);

        // Look (mouse delta)
        Math::Vector2 mouse_delta = input.GetMouseDelta();
        player_->SetLookInput(mouse_delta * 0.1f);

        // Actions
        player_->SetJumpInput(input.IsKeyDown(Input::KeyCode::Space));
        player_->SetSprintInput(input.IsKeyDown(Input::KeyCode::LeftShift));
        player_->SetCrouchInput(input.IsKeyDown(Input::KeyCode::LeftControl));
        player_->SetFireInput(input.IsMouseButtonDown(Input::MouseButton::Left));
        player_->SetAimInput(input.IsMouseButtonDown(Input::MouseButton::Right));

        if (input.IsKeyPressed(Input::KeyCode::R)) {
            player_->SetReloadInput(true);
        }

        // Weapon switching
        if (input.IsKeyPressed(Input::KeyCode::Num1)) {
            player_->SetWeaponSwitchInput(0);
        } else if (input.IsKeyPressed(Input::KeyCode::Num2)) {
            player_->SetWeaponSwitchInput(1);
        } else if (input.IsKeyPressed(Input::KeyCode::Num3)) {
            player_->SetWeaponSwitchInput(2);
        }

        // Debug toggles
        if (input.IsKeyPressed(Input::KeyCode::F1)) {
            Debug::StatsOverlay::Instance().SetVisible(
                !Debug::StatsOverlay::Instance().IsVisible()
            );
        }

        // Quit
        if (input.IsKeyPressed(Input::KeyCode::Escape)) {
            running_ = false;
        }
    }

    void Update(float32 delta_time) {
        // Update game world
        Game::GameWorld::Instance().Update(delta_time);

        // Update camera to follow player
        if (player_ && camera_) {
            camera_->SetPosition(player_->GetCameraPosition());

            float32 rad = 3.14159265359f / 180.0f;
            Math::Quaternion yaw_rot = Math::Quaternion::FromAxisAngle(
                Math::Vector3::Up, player_->GetYaw() * rad
            );
            Math::Quaternion pitch_rot = Math::Quaternion::FromAxisAngle(
                Math::Vector3::Right, player_->GetPitch() * rad
            );
            camera_->SetRotation(yaw_rot * pitch_rot);
            camera_->SetFieldOfView(player_->GetCurrentFOV());

            camera_->UpdateMatrices();
        }

        // Update terrain
        if (terrain_ && camera_) {
            terrain_->Update(camera_->GetPosition(), delta_time);
        }

        // Update debug overlay
        Debug::StatsOverlay::Instance().Update(delta_time);
    }

    void Render() {
        if (!camera_) return;

        // auto& renderer = Renderer::DeferredRenderer::Instance();
        // renderer.BeginFrame();

        // Render terrain
        // if (terrain_) {
        //     terrain_->Render(renderer.GetCommandBuffer(), camera_->GetViewProjectionMatrix());
        // }

        // Render game entities
        // for (auto& enemy : Game::GameWorld::Instance().GetEnemies()) {
        //     renderer.SubmitMesh(enemy->GetMesh(), enemy->GetPosition(), enemy->GetRotation());
        // }

        // Render current weapon
        // if (player_ && player_->GetCurrentWeapon()) {
        //     // Render weapon model
        // }

        // Debug rendering
        if (Debug::DebugRenderer::Instance().IsEnabled()) {
            // Draw enemy positions
            for (const auto& enemy : Game::GameWorld::Instance().GetEnemies()) {
                Debug::DebugRenderer::Instance().DrawWireSphere(
                    enemy->GetPosition(),
                    1.0f,
                    enemy->GetHealth().IsAlive() ? Math::Vector4(1, 0, 0, 1) : Math::Vector4(0.5f, 0.5f, 0.5f, 1)
                );
            }

            // Draw spawn points
            // Draw grid
            Debug::DebugRenderer::Instance().DrawGrid(
                Math::Vector3(128, 0, 128),
                256.0f,
                16
            );
        }

        Debug::DebugRenderer::Instance().Render(camera_->GetViewProjectionMatrix());

        // Render HUD
        RenderHUD();

        // Debug overlay
        Debug::StatsOverlay::Instance().Render();

        // renderer.EndFrame();
    }

    void RenderHUD() {
        if (!player_) return;

        // Would use UI system to render HUD elements
        // Health bar
        // Armor bar
        // Ammo counter
        // Crosshair
        // Wave info
        // Score

        // Example HUD data:
        // Health: player_->GetHealth().GetHealth()
        // Armor: player_->GetArmor()
        // Ammo: player_->GetCurrentWeapon()->GetAmmoInMagazine() / GetReserveAmmo()
        // Wave: survival_mode_->GetWaveNumber()
        // Score: survival_mode_->GetScore()
    }

    bool running_ = false;
    Game::Player* player_ = nullptr;
    Camera::Camera* camera_ = nullptr;
    Terrain::Terrain* terrain_ = nullptr;
    SurvivalGameMode* survival_mode_ = nullptr;
};

int main(int argc, char* argv[]) {
    FPSDemoApp app;

    if (!app.Initialize()) {
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
