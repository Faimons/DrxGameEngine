/**
 * DRX Engine - Camera System Implementation
 */

#include "Camera/Public/CameraSystem.h"
#include <cmath>
#include <algorithm>

namespace DRX::Camera {

// ============================================================================
// Camera
// ============================================================================

Camera::Camera() {
    UpdateMatrices();
}

void Camera::UpdateMatrices() {
    if (!matrices_dirty_) return;

    UpdateViewMatrix();
    UpdateProjectionMatrix();

    view_projection_matrix_ = projection_matrix_ * view_matrix_;
    matrices_dirty_ = false;
}

void Camera::UpdateViewMatrix() {
    Math::Vector3 forward = GetForward();
    Math::Vector3 right = GetRight();
    Math::Vector3 up = GetUp();

    view_matrix_ = Math::Matrix4(
        right.x, up.x, -forward.x, 0,
        right.y, up.y, -forward.y, 0,
        right.z, up.z, -forward.z, 0,
        -Math::Vector3::Dot(right, position_),
        -Math::Vector3::Dot(up, position_),
        Math::Vector3::Dot(forward, position_),
        1
    );
}

void Camera::UpdateProjectionMatrix() {
    if (projection_type_ == ProjectionType::Perspective) {
        float32 rad = fov_ * 3.14159265359f / 180.0f;
        float32 tanHalfFov = tan(rad / 2.0f);

        projection_matrix_ = Math::Matrix4::Zero();
        projection_matrix_.m[0][0] = 1.0f / (aspect_ratio_ * tanHalfFov);
        projection_matrix_.m[1][1] = 1.0f / tanHalfFov;
        projection_matrix_.m[2][2] = -(far_plane_ + near_plane_) / (far_plane_ - near_plane_);
        projection_matrix_.m[2][3] = -1.0f;
        projection_matrix_.m[3][2] = -(2.0f * far_plane_ * near_plane_) / (far_plane_ - near_plane_);
    } else {
        float32 right = ortho_size_ * aspect_ratio_;
        float32 left = -right;
        float32 top = ortho_size_;
        float32 bottom = -top;

        projection_matrix_ = Math::Matrix4::Identity();
        projection_matrix_.m[0][0] = 2.0f / (right - left);
        projection_matrix_.m[1][1] = 2.0f / (top - bottom);
        projection_matrix_.m[2][2] = -2.0f / (far_plane_ - near_plane_);
        projection_matrix_.m[3][0] = -(right + left) / (right - left);
        projection_matrix_.m[3][1] = -(top + bottom) / (top - bottom);
        projection_matrix_.m[3][2] = -(far_plane_ + near_plane_) / (far_plane_ - near_plane_);
    }
}

void Camera::SetPosition(const Math::Vector3& position) {
    position_ = position;
    matrices_dirty_ = true;
}

void Camera::SetRotation(const Math::Quaternion& rotation) {
    rotation_ = rotation;
    matrices_dirty_ = true;
}

Math::Vector3 Camera::GetForward() const {
    return rotation_.Rotate(Math::Vector3::Forward);
}

Math::Vector3 Camera::GetRight() const {
    return rotation_.Rotate(Math::Vector3::Right);
}

Math::Vector3 Camera::GetUp() const {
    return rotation_.Rotate(Math::Vector3::Up);
}

void Camera::LookAt(const Math::Vector3& target, const Math::Vector3& up) {
    Math::Vector3 forward = (target - position_).Normalized();
    Math::Vector3 right = Math::Vector3::Cross(up, forward).Normalized();
    Math::Vector3 actualUp = Math::Vector3::Cross(forward, right);

    rotation_ = Math::Quaternion::FromRotationMatrix(Math::Matrix4(
        right.x, actualUp.x, forward.x, 0,
        right.y, actualUp.y, forward.y, 0,
        right.z, actualUp.z, forward.z, 0,
        0, 0, 0, 1
    ));
    matrices_dirty_ = true;
}

void Camera::SetProjectionType(ProjectionType type) {
    projection_type_ = type;
    matrices_dirty_ = true;
}

void Camera::SetFieldOfView(float32 fov) {
    fov_ = fov;
    matrices_dirty_ = true;
}

void Camera::SetAspectRatio(float32 aspect) {
    aspect_ratio_ = aspect;
    matrices_dirty_ = true;
}

void Camera::SetOrthographicSize(float32 size) {
    ortho_size_ = size;
    matrices_dirty_ = true;
}

void Camera::SetNearPlane(float32 near_plane) {
    near_plane_ = near_plane;
    matrices_dirty_ = true;
}

void Camera::SetFarPlane(float32 far_plane) {
    far_plane_ = far_plane;
    matrices_dirty_ = true;
}

void Camera::SetViewport(float32 x, float32 y, float32 width, float32 height) {
    viewport_x_ = x;
    viewport_y_ = y;
    viewport_width_ = width;
    viewport_height_ = height;
}

Math::Vector3 Camera::ScreenToWorldPoint(const Math::Vector3& screen_pos) const {
    Math::Vector3 ndc;
    ndc.x = (screen_pos.x / viewport_width_) * 2.0f - 1.0f;
    ndc.y = 1.0f - (screen_pos.y / viewport_height_) * 2.0f;
    ndc.z = screen_pos.z * 2.0f - 1.0f;

    Math::Matrix4 inv_vp = view_projection_matrix_.Inverse();
    Math::Vector4 world = inv_vp * Math::Vector4(ndc, 1.0f);

    return Math::Vector3(world.x, world.y, world.z) / world.w;
}

Math::Vector3 Camera::WorldToScreenPoint(const Math::Vector3& world_pos) const {
    Math::Vector4 clip = view_projection_matrix_ * Math::Vector4(world_pos, 1.0f);
    Math::Vector3 ndc = Math::Vector3(clip.x, clip.y, clip.z) / clip.w;

    Math::Vector3 screen;
    screen.x = (ndc.x + 1.0f) * 0.5f * viewport_width_;
    screen.y = (1.0f - ndc.y) * 0.5f * viewport_height_;
    screen.z = (ndc.z + 1.0f) * 0.5f;

    return screen;
}

Math::Vector3 Camera::ScreenToViewportPoint(const Math::Vector3& screen_pos) const {
    return Math::Vector3(
        screen_pos.x / viewport_width_,
        screen_pos.y / viewport_height_,
        screen_pos.z
    );
}

Math::Vector3 Camera::ViewportToScreenPoint(const Math::Vector3& viewport_pos) const {
    return Math::Vector3(
        viewport_pos.x * viewport_width_,
        viewport_pos.y * viewport_height_,
        viewport_pos.z
    );
}

Camera::Ray Camera::ScreenPointToRay(const Math::Vector2& screen_pos) const {
    Math::Vector3 near_point = ScreenToWorldPoint(Math::Vector3(screen_pos.x, screen_pos.y, 0));
    Math::Vector3 far_point = ScreenToWorldPoint(Math::Vector3(screen_pos.x, screen_pos.y, 1));

    Ray ray;
    ray.origin = near_point;
    ray.direction = (far_point - near_point).Normalized();
    return ray;
}

// ============================================================================
// FPSCameraController
// ============================================================================

FPSCameraController::FPSCameraController(Camera* camera)
    : camera_(camera), current_distance_(distance) {
    if (camera_) {
        Math::Vector3 euler = camera_->GetRotation().ToEuler();
        yaw_ = euler.y;
        pitch_ = euler.x;
    }
}

void FPSCameraController::Update(float32 delta_time) {
    if (!camera_) return;

    // Update rotation from look input
    yaw_ += look_input_.x * look_sensitivity * (invert_y ? 1.0f : -1.0f);
    pitch_ += look_input_.y * look_sensitivity * (invert_y ? -1.0f : 1.0f);
    pitch_ = std::clamp(pitch_, min_pitch, max_pitch);

    // Calculate rotation quaternion
    float32 rad = 3.14159265359f / 180.0f;
    Math::Quaternion yaw_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw_ * rad);
    Math::Quaternion pitch_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, pitch_ * rad);
    Math::Quaternion rotation = yaw_rot * pitch_rot;

    camera_->SetRotation(rotation);

    // Calculate movement
    float32 current_speed = move_speed;
    if (sprint_input_) current_speed *= sprint_multiplier;
    if (crouch_input_) current_speed *= crouch_multiplier;

    Math::Vector3 forward = camera_->GetForward();
    Math::Vector3 right = camera_->GetRight();

    // Project to XZ plane for movement
    forward.y = 0;
    forward = forward.Normalized();
    right.y = 0;
    right = right.Normalized();

    Math::Vector3 target_velocity = (forward * move_input_.y + right * move_input_.x) * current_speed;

    // Smooth velocity
    velocity_ = Math::Vector3::Lerp(velocity_, target_velocity, smoothing * delta_time);

    Math::Vector3 pos = camera_->GetPosition();
    pos = pos + velocity_ * delta_time;
    camera_->SetPosition(pos);

    // Clear inputs
    look_input_ = Math::Vector2(0, 0);
}

// ============================================================================
// OrbitCameraController
// ============================================================================

OrbitCameraController::OrbitCameraController(Camera* camera)
    : camera_(camera), current_distance_(distance) {}

void OrbitCameraController::Update(float32 delta_time) {
    if (!camera_) return;

    // Update rotation from input
    yaw_ += rotate_input_.x * rotate_speed;
    pitch_ += rotate_input_.y * rotate_speed;
    pitch_ = std::clamp(pitch_, min_pitch, max_pitch);

    // Update distance from zoom input
    distance -= zoom_input_ * zoom_speed;
    distance = std::clamp(distance, min_distance, max_distance);

    // Smooth distance
    current_distance_ = current_distance_ + (distance - current_distance_) * smoothing * delta_time;

    // Pan the target
    if (pan_input_.LengthSquared() > 0.001f) {
        Math::Vector3 right = camera_->GetRight();
        Math::Vector3 up = camera_->GetUp();
        target_ = target_ + (right * -pan_input_.x + up * pan_input_.y) * pan_speed * delta_time;
    }

    // Calculate camera position
    float32 rad = 3.14159265359f / 180.0f;
    float32 cos_pitch = cos(pitch_ * rad);
    float32 sin_pitch = sin(pitch_ * rad);
    float32 cos_yaw = cos(yaw_ * rad);
    float32 sin_yaw = sin(yaw_ * rad);

    Math::Vector3 offset(
        current_distance_ * cos_pitch * sin_yaw,
        current_distance_ * sin_pitch,
        current_distance_ * cos_pitch * cos_yaw
    );

    camera_->SetPosition(target_ + offset);
    camera_->LookAt(target_);

    // Clear inputs
    rotate_input_ = Math::Vector2(0, 0);
    zoom_input_ = 0;
    pan_input_ = Math::Vector2(0, 0);
}

// ============================================================================
// FollowCameraController
// ============================================================================

FollowCameraController::FollowCameraController(Camera* camera)
    : camera_(camera), distance_(offset.Length()) {
    if (camera_) {
        current_position_ = camera_->GetPosition();
    }
}

void FollowCameraController::Update(float32 delta_time) {
    if (!camera_) return;

    // Update rotation from input
    yaw_ += look_input_.x * look_sensitivity;
    pitch_ += look_input_.y * look_sensitivity;
    pitch_ = std::clamp(pitch_, min_pitch, max_pitch);

    // Update distance from zoom
    distance_ -= zoom_input_ * zoom_speed * delta_time;
    distance_ = std::clamp(distance_, min_distance, max_distance);

    // Calculate desired position
    float32 rad = 3.14159265359f / 180.0f;
    Math::Quaternion yaw_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw_ * rad);
    Math::Quaternion pitch_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, pitch_ * rad);
    Math::Quaternion rotation = yaw_rot * pitch_rot;

    Math::Vector3 target_pos = target_transform_.position;
    Math::Vector3 offset_rotated = rotation.Rotate(Math::Vector3(0, offset.y, -distance_));
    Math::Vector3 desired_pos = target_pos + offset_rotated;

    // Smooth position
    current_position_ = Math::Vector3::Lerp(current_position_, desired_pos, smoothing * delta_time);

    // Avoid obstacles if enabled
    if (avoid_obstacles) {
        AvoidObstacles();
    }

    camera_->SetPosition(current_position_);
    camera_->LookAt(target_pos + Math::Vector3(0, offset.y * 0.5f, 0));

    // Clear inputs
    look_input_ = Math::Vector2(0, 0);
    zoom_input_ = 0;
}

void FollowCameraController::AvoidObstacles() {
    // Raycast from target to camera
    // If hit, move camera closer
    // This would integrate with physics system for actual raycast
}

// ============================================================================
// FreeLookCameraController
// ============================================================================

FreeLookCameraController::FreeLookCameraController(Camera* camera)
    : camera_(camera) {
    if (camera_) {
        Math::Vector3 euler = camera_->GetRotation().ToEuler();
        yaw_ = euler.y;
        pitch_ = euler.x;
    }
}

void FreeLookCameraController::Update(float32 delta_time) {
    if (!camera_) return;

    // Update rotation
    yaw_ += look_input_.x * look_sensitivity;
    pitch_ += look_input_.y * look_sensitivity;
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);

    float32 rad = 3.14159265359f / 180.0f;
    Math::Quaternion yaw_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw_ * rad);
    Math::Quaternion pitch_rot = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, pitch_ * rad);
    camera_->SetRotation(yaw_rot * pitch_rot);

    // Calculate speed
    float32 current_speed = base_speed * speed_modifier_;
    if (speed_modifier_ > 1.5f) current_speed = fast_speed;
    if (speed_modifier_ < 0.5f) current_speed = slow_speed;

    // Calculate target velocity
    Math::Vector3 forward = camera_->GetForward();
    Math::Vector3 right = camera_->GetRight();
    Math::Vector3 up = Math::Vector3::Up;

    Math::Vector3 target_velocity = (
        forward * move_input_.z +
        right * move_input_.x +
        up * move_input_.y
    ) * current_speed;

    // Accelerate/decelerate
    if (target_velocity.LengthSquared() > 0.001f) {
        velocity_ = Math::Vector3::Lerp(velocity_, target_velocity, acceleration * delta_time);
    } else {
        velocity_ = Math::Vector3::Lerp(velocity_, Math::Vector3::Zero, deceleration * delta_time);
    }

    Math::Vector3 pos = camera_->GetPosition();
    pos = pos + velocity_ * delta_time;
    camera_->SetPosition(pos);

    // Clear inputs
    look_input_ = Math::Vector2(0, 0);
}

void FreeLookCameraController::SetFocusPoint(const Math::Vector3& point) {
    if (!camera_) return;

    camera_->LookAt(point);

    Math::Vector3 euler = camera_->GetRotation().ToEuler();
    yaw_ = euler.y;
    pitch_ = euler.x;
}

// ============================================================================
// CinematicCamera
// ============================================================================

CinematicCamera::CinematicCamera(Camera* camera)
    : camera_(camera) {}

void CinematicCamera::Update(float32 delta_time) {
    if (!camera_ || !playing_ || keyframes_.empty()) return;

    current_time_ += delta_time * playback_speed;

    float32 duration = GetDuration();
    if (current_time_ >= duration) {
        if (loop) {
            current_time_ = fmod(current_time_, duration);
        } else {
            current_time_ = duration;
            playing_ = false;
        }
    }

    Keyframe kf = Evaluate(current_time_);
    camera_->SetPosition(kf.position);
    camera_->SetRotation(kf.rotation);
    camera_->SetFieldOfView(kf.fov);
}

void CinematicCamera::AddKeyframe(const Keyframe& keyframe) {
    keyframes_.push_back(keyframe);

    // Sort by time
    std::sort(keyframes_.begin(), keyframes_.end(),
        [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

void CinematicCamera::RemoveKeyframe(uint32 index) {
    if (index < keyframes_.size()) {
        keyframes_.erase(keyframes_.begin() + index);
    }
}

void CinematicCamera::ClearKeyframes() {
    keyframes_.clear();
}

void CinematicCamera::Play() {
    playing_ = true;
}

void CinematicCamera::Pause() {
    playing_ = false;
}

void CinematicCamera::Stop() {
    playing_ = false;
    current_time_ = 0;
}

void CinematicCamera::SetTime(float32 time) {
    current_time_ = std::clamp(time, 0.0f, GetDuration());
}

float32 CinematicCamera::GetDuration() const {
    if (keyframes_.empty()) return 0;
    return keyframes_.back().time;
}

CinematicCamera::Keyframe CinematicCamera::Evaluate(float32 time) const {
    if (keyframes_.empty()) return {};
    if (keyframes_.size() == 1) return keyframes_[0];

    // Find surrounding keyframes
    size_t next_idx = 0;
    for (size_t i = 0; i < keyframes_.size(); ++i) {
        if (keyframes_[i].time > time) {
            next_idx = i;
            break;
        }
        next_idx = i;
    }

    size_t prev_idx = next_idx > 0 ? next_idx - 1 : 0;

    if (prev_idx == next_idx) return keyframes_[prev_idx];

    const Keyframe& prev = keyframes_[prev_idx];
    const Keyframe& next = keyframes_[next_idx];

    float32 t = (time - prev.time) / (next.time - prev.time);

    // Get control points for Catmull-Rom spline
    size_t p0_idx = prev_idx > 0 ? prev_idx - 1 : prev_idx;
    size_t p3_idx = next_idx < keyframes_.size() - 1 ? next_idx + 1 : next_idx;

    Keyframe result;
    result.position = CatmullRomSpline(
        keyframes_[p0_idx].position,
        prev.position,
        next.position,
        keyframes_[p3_idx].position,
        t
    );
    result.rotation = Math::Quaternion::Slerp(prev.rotation, next.rotation, t);
    result.fov = prev.fov + (next.fov - prev.fov) * t;
    result.time = time;

    return result;
}

Math::Vector3 CinematicCamera::CatmullRomSpline(const Math::Vector3& p0, const Math::Vector3& p1,
                                                  const Math::Vector3& p2, const Math::Vector3& p3,
                                                  float32 t) const {
    float32 t2 = t * t;
    float32 t3 = t2 * t;

    return (
        (p1 * 2.0f) +
        (p2 - p0) * t +
        (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
        (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3
    ) * 0.5f;
}

// ============================================================================
// CameraShake
// ============================================================================

void CameraShake::AddShake(const ShakeParams& params) {
    ActiveShake shake;
    shake.params = params;
    shake.time_remaining = params.duration;
    active_shakes_.push_back(shake);
}

void CameraShake::Update(float32 delta_time) {
    position_offset_ = Math::Vector3::Zero;
    rotation_offset_ = Math::Vector3::Zero;

    noise_seed_ += delta_time;

    // Update each active shake
    for (auto it = active_shakes_.begin(); it != active_shakes_.end();) {
        it->time_remaining -= delta_time;

        if (it->time_remaining <= 0) {
            it = active_shakes_.erase(it);
            continue;
        }

        float32 progress = 1.0f - (it->time_remaining / it->params.duration);
        float32 decay = 1.0f - (progress * it->params.decay);
        decay = std::max(0.0f, decay);

        float32 magnitude = it->params.magnitude * decay;
        float32 frequency = it->params.frequency;

        // Generate noise-based shake using simple sin functions
        float32 time = noise_seed_ * frequency;

        if (it->params.position_shake) {
            position_offset_.x += sin(time * 1.1f) * magnitude * it->params.position_influence.x;
            position_offset_.y += sin(time * 1.3f) * magnitude * it->params.position_influence.y;
            position_offset_.z += sin(time * 0.9f) * magnitude * it->params.position_influence.z;
        }

        if (it->params.rotation_shake) {
            rotation_offset_.x += sin(time * 1.2f) * magnitude * 5.0f * it->params.rotation_influence.x;
            rotation_offset_.y += sin(time * 1.4f) * magnitude * 5.0f * it->params.rotation_influence.y;
            rotation_offset_.z += sin(time * 0.8f) * magnitude * 5.0f * it->params.rotation_influence.z;
        }

        ++it;
    }
}

void CameraShake::Clear() {
    active_shakes_.clear();
    position_offset_ = Math::Vector3::Zero;
    rotation_offset_ = Math::Vector3::Zero;
}

// ============================================================================
// CameraManager
// ============================================================================

CameraManager& CameraManager::Instance() {
    static CameraManager instance;
    return instance;
}

void CameraManager::Initialize() {
    // Create default main camera
    main_camera_ = CreateCamera("Main");
    active_cameras_.push_back(main_camera_);
}

void CameraManager::Shutdown() {
    cameras_.clear();
    main_camera_ = nullptr;
    active_cameras_.clear();
}

void CameraManager::Update(float32 delta_time) {
    // Update camera blending
    if (blending_ && blend_from_ && blend_to_) {
        blend_time_ += delta_time;
        float32 t = std::min(blend_time_ / blend_duration_, 1.0f);

        // Smooth step interpolation
        t = t * t * (3.0f - 2.0f * t);

        // Interpolate position and rotation
        Math::Vector3 pos = Math::Vector3::Lerp(
            blend_from_->GetPosition(),
            blend_to_->GetPosition(),
            t
        );
        Math::Quaternion rot = Math::Quaternion::Slerp(
            blend_from_->GetRotation(),
            blend_to_->GetRotation(),
            t
        );
        float32 fov = blend_from_->GetFieldOfView() +
                      (blend_to_->GetFieldOfView() - blend_from_->GetFieldOfView()) * t;

        if (main_camera_ && main_camera_ != blend_from_ && main_camera_ != blend_to_) {
            main_camera_->SetPosition(pos);
            main_camera_->SetRotation(rot);
            main_camera_->SetFieldOfView(fov);
        }

        if (t >= 1.0f) {
            blending_ = false;
            SetMainCamera(blend_to_);
        }
    }

    // Update all cameras
    for (auto& [name, camera] : cameras_) {
        camera->UpdateMatrices();
    }
}

Camera* CameraManager::CreateCamera(const String& name) {
    auto camera = std::make_unique<Camera>();
    Camera* ptr = camera.get();
    cameras_[name] = std::move(camera);
    return ptr;
}

void CameraManager::DestroyCamera(const String& name) {
    auto it = cameras_.find(name);
    if (it != cameras_.end()) {
        Camera* cam = it->second.get();

        // Remove from active cameras
        active_cameras_.erase(
            std::remove(active_cameras_.begin(), active_cameras_.end(), cam),
            active_cameras_.end()
        );

        // Clear main camera if needed
        if (main_camera_ == cam) {
            main_camera_ = active_cameras_.empty() ? nullptr : active_cameras_[0];
        }

        cameras_.erase(it);
    }
}

Camera* CameraManager::GetCamera(const String& name) {
    auto it = cameras_.find(name);
    return it != cameras_.end() ? it->second.get() : nullptr;
}

void CameraManager::SetMainCamera(Camera* camera) {
    main_camera_ = camera;
}

void CameraManager::AddActiveCamera(Camera* camera) {
    if (camera && std::find(active_cameras_.begin(), active_cameras_.end(), camera) == active_cameras_.end()) {
        active_cameras_.push_back(camera);
    }
}

void CameraManager::RemoveActiveCamera(Camera* camera) {
    active_cameras_.erase(
        std::remove(active_cameras_.begin(), active_cameras_.end(), camera),
        active_cameras_.end()
    );
}

void CameraManager::BlendToCamera(Camera* target, float32 duration) {
    if (!main_camera_ || !target || main_camera_ == target) return;

    blend_from_ = main_camera_;
    blend_to_ = target;
    blend_time_ = 0;
    blend_duration_ = duration;
    blending_ = true;
}

} // namespace DRX::Camera
