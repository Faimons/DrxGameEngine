/**
 * DRX Engine - Audio System
 * OpenAL-based 3D audio with streaming support
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"

// Forward declare OpenAL types
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

namespace DRX::Audio {

/**
 * Audio file formats
 */
enum class AudioFormat : uint32 {
    Unknown,
    WAV,
    OGG,
    MP3,
    FLAC
};

/**
 * Sound attenuation models
 */
enum class AttenuationModel : uint32 {
    None,           // No distance attenuation
    Inverse,        // 1 / distance
    InverseClamped, // Clamped inverse
    Linear,         // Linear falloff
    LinearClamped,  // Clamped linear
    Exponential,    // Exponential falloff
    ExponentialClamped
};

/**
 * Audio clip - loaded sound data
 */
class AudioClip {
public:
    AudioClip() = default;
    ~AudioClip();

    bool LoadFromFile(const String& path);
    bool LoadFromMemory(const void* data, uint32 size, AudioFormat format);
    void Unload();

    bool IsLoaded() const { return buffer_ != 0; }
    float32 GetDuration() const { return duration_; }
    uint32 GetSampleRate() const { return sample_rate_; }
    uint32 GetChannels() const { return channels_; }
    const String& GetName() const { return name_; }

    uint32 GetBuffer() const { return buffer_; }

private:
    bool LoadWAV(const String& path);
    bool LoadOGG(const String& path);

    uint32 buffer_ = 0;
    String name_;
    float32 duration_ = 0.0f;
    uint32 sample_rate_ = 0;
    uint32 channels_ = 0;
};

/**
 * Audio source - plays audio clips
 */
class AudioSource {
public:
    AudioSource() = default;
    ~AudioSource();

    bool Create();
    void Destroy();

    // Playback
    void Play(SharedPtr<AudioClip> clip);
    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const;
    bool IsPaused() const;

    // Properties
    void SetVolume(float32 volume);
    float32 GetVolume() const { return volume_; }

    void SetPitch(float32 pitch);
    float32 GetPitch() const { return pitch_; }

    void SetLooping(bool loop);
    bool IsLooping() const { return looping_; }

    // 3D Audio
    void SetPosition(const Math::Vector3& position);
    Math::Vector3 GetPosition() const { return position_; }

    void SetVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetVelocity() const { return velocity_; }

    void SetDirection(const Math::Vector3& direction);

    // Attenuation
    void SetMinDistance(float32 distance);
    void SetMaxDistance(float32 distance);
    void SetRolloffFactor(float32 factor);

    // Cone (for directional sounds)
    void SetConeAngles(float32 inner_degrees, float32 outer_degrees);
    void SetConeOuterGain(float32 gain);

    // Time
    float32 GetPlaybackPosition() const;
    void SetPlaybackPosition(float32 seconds);

    uint32 GetSource() const { return source_; }

private:
    uint32 source_ = 0;
    SharedPtr<AudioClip> current_clip_;

    float32 volume_ = 1.0f;
    float32 pitch_ = 1.0f;
    bool looping_ = false;

    Math::Vector3 position_;
    Math::Vector3 velocity_;
};

/**
 * Audio Listener - represents the "ears" in the scene
 */
class AudioListener {
public:
    AudioListener() = default;

    void SetPosition(const Math::Vector3& position);
    Math::Vector3 GetPosition() const { return position_; }

    void SetVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetVelocity() const { return velocity_; }

    void SetOrientation(const Math::Vector3& forward, const Math::Vector3& up);

    void SetGain(float32 gain);
    float32 GetGain() const { return gain_; }

private:
    Math::Vector3 position_;
    Math::Vector3 velocity_;
    Math::Vector3 forward_ = {0, 0, -1};
    Math::Vector3 up_ = {0, 1, 0};
    float32 gain_ = 1.0f;
};

/**
 * Streaming Audio Source
 * For long audio files (music, ambient)
 */
class StreamingAudioSource {
public:
    static constexpr uint32 NUM_BUFFERS = 4;
    static constexpr uint32 BUFFER_SIZE = 65536;

    StreamingAudioSource() = default;
    ~StreamingAudioSource();

    bool Create();
    void Destroy();

    bool OpenFile(const String& path);
    void Close();

    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const;

    void Update();  // Call each frame to refill buffers

    void SetVolume(float32 volume);
    void SetLooping(bool loop);

private:
    bool FillBuffer(uint32 buffer);

    uint32 source_ = 0;
    uint32 buffers_[NUM_BUFFERS] = {};
    void* decoder_ = nullptr;  // OGG/MP3 decoder handle

    String file_path_;
    bool looping_ = false;
    float32 volume_ = 1.0f;
    bool playing_ = false;
};

/**
 * Audio Mixer Channel
 */
struct MixerChannel {
    String name;
    float32 volume = 1.0f;
    bool muted = false;
    Array<SharedPtr<AudioSource>> sources;
};

/**
 * Audio System
 * Main audio management class
 */
class AudioSystem {
public:
    static AudioSystem& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    // Listener
    AudioListener& GetListener() { return listener_; }

    // Audio clips
    SharedPtr<AudioClip> LoadClip(const String& path);
    SharedPtr<AudioClip> GetClip(const String& name);
    void UnloadClip(const String& name);
    void UnloadAllClips();

    // Audio sources
    SharedPtr<AudioSource> CreateSource();
    void DestroySource(SharedPtr<AudioSource> source);

    // Streaming sources
    SharedPtr<StreamingAudioSource> CreateStreamingSource();
    void DestroyStreamingSource(SharedPtr<StreamingAudioSource> source);

    // Quick play (fire and forget)
    void PlayOneShot(SharedPtr<AudioClip> clip, const Math::Vector3& position,
                     float32 volume = 1.0f);
    void PlayOneShot2D(SharedPtr<AudioClip> clip, float32 volume = 1.0f);

    // Mixer
    void CreateChannel(const String& name);
    void SetChannelVolume(const String& name, float32 volume);
    void SetChannelMuted(const String& name, bool muted);
    float32 GetChannelVolume(const String& name) const;

    // Master volume
    void SetMasterVolume(float32 volume);
    float32 GetMasterVolume() const { return master_volume_; }

    // Global settings
    void SetDistanceModel(AttenuationModel model);
    void SetDopplerFactor(float32 factor);
    void SetSpeedOfSound(float32 speed);

    // Pause all
    void PauseAll();
    void ResumeAll();
    void StopAll();

private:
    AudioSystem() = default;
    ~AudioSystem() = default;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void CleanupFinishedSources();

    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    AudioListener listener_;

    HashMap<String, SharedPtr<AudioClip>> clips_;
    Array<SharedPtr<AudioSource>> sources_;
    Array<SharedPtr<StreamingAudioSource>> streaming_sources_;
    Array<SharedPtr<AudioSource>> oneshot_sources_;

    HashMap<String, MixerChannel> mixer_channels_;
    float32 master_volume_ = 1.0f;

    bool initialized_ = false;
};

} // namespace DRX::Audio
