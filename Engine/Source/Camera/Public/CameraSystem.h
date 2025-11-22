/**
 * DRX Engine - Camera System
 * Various camera types and controllers
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Matrix4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"

namespace DRX::Camera {

/**
 * Camera projection type
 */
enum class ProjectionType : uint32 {
    Perspective,
    Orthographic
};

/**
 * Camera clear flags
 */
enum class ClearFlags : uint32 {
    Skybox = 0,
    SolidColor = 1,
    DepthOnly = 2,
    Nothing = 3
};

/**
 * Base Camera class
 */
class Camera {
public:
    Camera();
    virtual ~Camera() = default;

    // Update matrices
    virtual void UpdateMatrices();

    // Matrices
    const Math::Matrix4& GetViewMatrix() const { return view_matrix_; }
    const Math::Matrix4& GetProjectionMatrix() const { return projection_matrix_; }
    const Math::Matrix4& GetViewProjectionMatrix() const { return view_projection_matrix_; }

    // Transform
    const Math::Vector3& GetPosition() const { return position_; }
    void SetPosition(const Math::Vector3& position);

    const Math::Quaternion& GetRotation() const { return rotation_; }
    void SetRotation(const Math::Quaternion& rotation);

    Math::Vector3 GetForward() const;
    Math::Vector3 GetRight() const;
    Math::Vector3 GetUp() const;

    void LookAt(const Math::Vector3& target, const Math::Vector3& up = Math::Vector3::Up);

    // Projection
    ProjectionType GetProjectionType() const { return projection_type_; }
    void SetProjectionType(ProjectionType type);

    // Perspective settings
    float32 GetFieldOfView() const { return fov_; }
    void SetFieldOfView(float32 fov);

    float32 GetAspectRatio() const { return aspect_ratio_; }
    void SetAspectRatio(float32 aspect);

    // Orthographic settings
    float32 GetOrthographicSize() const { return ortho_size_; }
    void SetOrthographicSize(float32 size);

    // Common settings
    float32 GetNearPlane() const { return near_plane_; }
    void SetNearPlane(float32 near_plane);

    float32 GetFarPlane() const { return far_plane_; }
    void SetFarPlane(float32 far_plane);

    // Clear settings
    ClearFlags GetClearFlags() const { return clear_flags_; }
    void SetClearFlags(ClearFlags flags) { clear_flags_ = flags; }

    Math::Vector4 GetClearColor() const { return clear_color_; }
    void SetClearColor(const Math::Vector4& color) { clear_color_ = color; }

    // Viewport
    void SetViewport(float32 x, float32 y, float32 width, float32 height);

    // Depth
    int32 GetDepth() const { return depth_; }
    void SetDepth(int32 depth) { depth_ = depth; }

    // Culling mask
    uint32 GetCullingMask() const { return culling_mask_; }
    void SetCullingMask(uint32 mask) { culling_mask_ = mask; }

    // Utility
    Math::Vector3 ScreenToWorldPoint(const Math::Vector3& screen_pos) const;
    Math::Vector3 WorldToScreenPoint(const Math::Vector3& world_pos) const;
    Math::Vector3 ScreenToViewportPoint(const Math::Vector3& screen_pos) const;
    Math::Vector3 ViewportToScreenPoint(const Math::Vector3& viewport_pos) const;

    struct Ray {
        Math::Vector3 origin;
        Math::Vector3 direction;
    };
    Ray ScreenPointToRay(const Math::Vector2& screen_pos) const;

protected:
    void UpdateViewMatrix();
    void UpdateProjectionMatrix();

    // Transform
    Math::Vector3 position_ = {0, 0, 0};
    Math::Quaternion rotation_ = {};

    // Matrices
    Math::Matrix4 view_matrix_;
    Math::Matrix4 projection_matrix_;
    Math::Matrix4 view_projection_matrix_;

    // Projection
    ProjectionType projection_type_ = ProjectionType::Perspective;
    float32 fov_ = 60.0f;
    float32 aspect_ratio_ = 16.0f / 9.0f;
    float32 ortho_size_ = 5.0f;
    float32 near_plane_ = 0.1f;
    float32 far_plane_ = 1000.0f;

    // Viewport
    float32 viewport_x_ = 0;
    float32 viewport_y_ = 0;
    float32 viewport_width_ = 1;
    float32 viewport_height_ = 1;

    // Clear
    ClearFlags clear_flags_ = ClearFlags::Skybox;
    Math::Vector4 clear_color_ = {0.1f, 0.1f, 0.1f, 1.0f};

    // Rendering
    int32 depth_ = 0;
    uint32 culling_mask_ = 0xFFFFFFFF;

    bool matrices_dirty_ = true;
};

/**
 * FPS Camera Controller
 */
class FPSCameraController {
public:
    FPSCameraController(Camera* camera);

    void Update(float32 delta_time);

    // Input
    void SetMoveInput(const Math::Vector2& input) { move_input_ = input; }
    void SetLookInput(const Math::Vector2& input) { look_input_ = input; }
    void SetJumpInput(bool jump) { jump_input_ = jump; }
    void SetSprintInput(bool sprint) { sprint_input_ = sprint; }
    void SetCrouchInput(bool crouch) { crouch_input_ = crouch; }

    // Settings
    float32 move_speed = 5.0f;
    float32 sprint_multiplier = 2.0f;
    float32 crouch_multiplier = 0.5f;
    float32 look_sensitivity = 0.1f;
    float32 smoothing = 10.0f;

    float32 min_pitch = -89.0f;
    float32 max_pitch = 89.0f;

    bool invert_y = false;

private:
    Camera* camera_;

    Math::Vector2 move_input_;
    Math::Vector2 look_input_;
    bool jump_input_ = false;
    bool sprint_input_ = false;
    bool crouch_input_ = false;

    float32 yaw_ = 0;
    float32 pitch_ = 0;
    Math::Vector3 velocity_;
};

/**
 * Orbit Camera Controller
 */
class OrbitCameraController {
public:
    OrbitCameraController(Camera* camera);

    void Update(float32 delta_time);

    // Target
    void SetTarget(const Math::Vector3& target) { target_ = target; }
    const Math::Vector3& GetTarget() const { return target_; }

    // Input
    void SetRotateInput(const Math::Vector2& input) { rotate_input_ = input; }
    void SetZoomInput(float32 input) { zoom_input_ = input; }
    void SetPanInput(const Math::Vector2& input) { pan_input_ = input; }

    // Settings
    float32 distance = 10.0f;
    float32 min_distance = 1.0f;
    float32 max_distance = 100.0f;

    float32 rotate_speed = 1.0f;
    float32 zoom_speed = 1.0f;
    float32 pan_speed = 1.0f;
    float32 smoothing = 10.0f;

    float32 min_pitch = -89.0f;
    float32 max_pitch = 89.0f;

private:
    Camera* camera_;
    Math::Vector3 target_;

    Math::Vector2 rotate_input_;
    float32 zoom_input_ = 0;
    Math::Vector2 pan_input_;

    float32 yaw_ = 0;
    float32 pitch_ = 30.0f;
    float32 current_distance_;
};

/**
 * Follow Camera Controller (Third Person)
 */
class FollowCameraController {
public:
    FollowCameraController(Camera* camera);

    void Update(float32 delta_time);

    // Target
    void SetTarget(const Math::Transform& target) { target_transform_ = target; }

    // Input
    void SetLookInput(const Math::Vector2& input) { look_input_ = input; }
    void SetZoomInput(float32 input) { zoom_input_ = input; }

    // Settings
    Math::Vector3 offset = {0, 2.0f, -5.0f};
    float32 min_distance = 2.0f;
    float32 max_distance = 15.0f;

    float32 look_sensitivity = 1.0f;
    float32 zoom_speed = 2.0f;
    float32 smoothing = 5.0f;

    float32 min_pitch = -30.0f;
    float32 max_pitch = 60.0f;

    // Collision avoidance
    bool avoid_obstacles = true;
    float32 collision_radius = 0.3f;
    uint32 collision_layers = 0xFFFFFFFF;

private:
    void AvoidObstacles();

    Camera* camera_;
    Math::Transform target_transform_;

    Math::Vector2 look_input_;
    float32 zoom_input_ = 0;

    float32 yaw_ = 0;
    float32 pitch_ = 15.0f;
    float32 distance_;

    Math::Vector3 current_position_;
};

/**
 * Free Look Camera (Editor style)
 */
class FreeLookCameraController {
public:
    FreeLookCameraController(Camera* camera);

    void Update(float32 delta_time);

    // Input
    void SetMoveInput(const Math::Vector3& input) { move_input_ = input; }
    void SetLookInput(const Math::Vector2& input) { look_input_ = input; }
    void SetSpeedModifier(float32 modifier) { speed_modifier_ = modifier; }
    void SetFocusPoint(const Math::Vector3& point);

    // Settings
    float32 base_speed = 5.0f;
    float32 fast_speed = 20.0f;
    float32 slow_speed = 1.0f;
    float32 look_sensitivity = 0.2f;
    float32 smoothing = 15.0f;
    float32 acceleration = 10.0f;
    float32 deceleration = 10.0f;

private:
    Camera* camera_;

    Math::Vector3 move_input_;
    Math::Vector2 look_input_;
    float32 speed_modifier_ = 1.0f;

    float32 yaw_ = 0;
    float32 pitch_ = 0;
    Math::Vector3 velocity_;
};

/**
 * Cinematic Camera (Spline-based)
 */
class CinematicCamera {
public:
    struct Keyframe {
        Math::Vector3 position;
        Math::Quaternion rotation;
        float32 fov;
        float32 time;

        // Interpolation handles
        Math::Vector3 tangent_in;
        Math::Vector3 tangent_out;
    };

    CinematicCamera(Camera* camera);

    void Update(float32 delta_time);

    // Keyframes
    void AddKeyframe(const Keyframe& keyframe);
    void RemoveKeyframe(uint32 index);
    void ClearKeyframes();
    const Array<Keyframe>& GetKeyframes() const { return keyframes_; }

    // Playback
    void Play();
    void Pause();
    void Stop();
    void SetTime(float32 time);

    bool IsPlaying() const { return playing_; }
    float32 GetCurrentTime() const { return current_time_; }
    float32 GetDuration() const;

    // Settings
    bool loop = false;
    float32 playback_speed = 1.0f;

private:
    Keyframe Evaluate(float32 time) const;
    Math::Vector3 CatmullRomSpline(const Math::Vector3& p0, const Math::Vector3& p1,
                                     const Math::Vector3& p2, const Math::Vector3& p3, float32 t) const;

    Camera* camera_;
    Array<Keyframe> keyframes_;

    bool playing_ = false;
    float32 current_time_ = 0;
};

/**
 * Camera shake effect
 */
class CameraShake {
public:
    struct ShakeParams {
        float32 duration = 0.5f;
        float32 magnitude = 0.1f;
        float32 frequency = 25.0f;
        float32 decay = 1.0f;  // How quickly shake fades

        bool position_shake = true;
        bool rotation_shake = true;
        Math::Vector3 position_influence = {1, 1, 1};
        Math::Vector3 rotation_influence = {1, 1, 0};
    };

    void AddShake(const ShakeParams& params);
    void Update(float32 delta_time);
    void Clear();

    Math::Vector3 GetPositionOffset() const { return position_offset_; }
    Math::Vector3 GetRotationOffset() const { return rotation_offset_; }

private:
    struct ActiveShake {
        ShakeParams params;
        float32 time_remaining;
    };

    Array<ActiveShake> active_shakes_;
    Math::Vector3 position_offset_;
    Math::Vector3 rotation_offset_;
    float32 noise_seed_ = 0;
};

/**
 * Camera Manager
 */
class CameraManager {
public:
    static CameraManager& Instance();

    void Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    // Camera management
    Camera* CreateCamera(const String& name);
    void DestroyCamera(const String& name);
    Camera* GetCamera(const String& name);

    // Main camera
    void SetMainCamera(Camera* camera);
    Camera* GetMainCamera() const { return main_camera_; }

    // Active cameras (for split-screen, etc.)
    void AddActiveCamera(Camera* camera);
    void RemoveActiveCamera(Camera* camera);
    const Array<Camera*>& GetActiveCameras() const { return active_cameras_; }

    // Blending between cameras
    void BlendToCamera(Camera* target, float32 duration);
    bool IsBlending() const { return blending_; }

private:
    CameraManager() = default;

    HashMap<String, UniquePtr<Camera>> cameras_;
    Camera* main_camera_ = nullptr;
    Array<Camera*> active_cameras_;

    // Blending
    bool blending_ = false;
    Camera* blend_from_ = nullptr;
    Camera* blend_to_ = nullptr;
    float32 blend_time_ = 0;
    float32 blend_duration_ = 0;
};

} // namespace DRX::Camera
