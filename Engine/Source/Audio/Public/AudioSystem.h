/**
 * DRX Engine - Audio System
 * OpenAL-based 3D audio with mixer channels and streaming.
 * Decoding (WAV, OGG) works without an audio device; playback calls are no-ops when
 * no device is available, so games run unchanged on machines without sound.
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"

// Forward declare OpenAL types
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

namespace DRX::Audio {

enum class AudioFormat : uint32 {
    Unknown,
    WAV,
    OGG
};

enum class AttenuationModel : uint32 {
    None,
    Inverse,
    InverseClamped,
    Linear,
    LinearClamped,
    Exponential,
    ExponentialClamped
};

/**
 * Decoded audio: interleaved signed 16-bit samples
 */
struct PcmData {
    Array<int16> samples;
    uint32 channels = 0;
    uint32 sample_rate = 0;

    size_t GetFrameCount() const { return channels ? samples.size() / channels : 0; }
    float32 GetDuration() const {
        return sample_rate ? static_cast<float32>(GetFrameCount()) / static_cast<float32>(sample_rate) : 0.0f;
    }
};

AudioFormat DetectAudioFormat(const void* data, size_t size);
// Decodes WAV (PCM 8/16/24/32-bit, IEEE float) or OGG Vorbis into 16-bit PCM
bool DecodeAudio(const void* data, size_t size, AudioFormat format, PcmData& out, String& error);

/**
 * Audio clip - sound data in an OpenAL buffer
 */
class AudioClip {
public:
    AudioClip() = default;
    ~AudioClip();

    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    bool LoadFromFile(const String& path);
    bool LoadFromMemory(const void* data, size_t size, AudioFormat format = AudioFormat::Unknown,
                        const String& name = {});
    bool LoadFromPCM(const PcmData& pcm, const String& name = {});
    void Unload();

    bool IsLoaded() const { return buffer_ != 0; }
    float32 GetDuration() const { return duration_; }
    uint32 GetSampleRate() const { return sample_rate_; }
    uint32 GetChannels() const { return channels_; }
    const String& GetName() const { return name_; }
    uint32 GetBuffer() const { return buffer_; }

private:
    uint32 buffer_ = 0;
    String name_;
    float32 duration_ = 0.0f;
    uint32 sample_rate_ = 0;
    uint32 channels_ = 0;
};

/**
 * Audio source - plays clips. Spatialization applies to mono clips only (OpenAL rule).
 */
class AudioSource {
public:
    AudioSource() = default;
    ~AudioSource();

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    bool Create();
    void Destroy();
    bool IsValid() const { return source_ != 0; }

    // Playback
    void Play(SharedPtr<AudioClip> clip);
    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const;
    bool IsPaused() const;

    // Mixer channel ("Master", "Music", "SFX", "Voice", "Ambient" or a custom one)
    void SetChannel(const String& channel);
    const String& GetChannel() const { return channel_; }

    void SetVolume(float32 volume);
    float32 GetVolume() const { return volume_; }
    void SetPitch(float32 pitch);
    float32 GetPitch() const { return pitch_; }
    void SetLooping(bool loop);
    bool IsLooping() const { return looping_; }

    // 3D audio; relative sources are positioned relative to the listener (2D sounds)
    void SetRelative(bool relative);
    void SetPosition(const Math::Vector3& position);
    Math::Vector3 GetPosition() const { return position_; }
    void SetVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetVelocity() const { return velocity_; }
    void SetDirection(const Math::Vector3& direction);

    void SetMinDistance(float32 distance);
    void SetMaxDistance(float32 distance);
    void SetRolloffFactor(float32 factor);
    void SetConeAngles(float32 inner_degrees, float32 outer_degrees);
    void SetConeOuterGain(float32 gain);

    float32 GetPlaybackPosition() const;
    void SetPlaybackPosition(float32 seconds);

    // Re-applies volume x channel gain (called by the mixer)
    void ApplyGain();
    uint32 GetSource() const { return source_; }

private:
    uint32 source_ = 0;
    SharedPtr<AudioClip> current_clip_;
    String channel_ = "SFX";

    float32 volume_ = 1.0f;
    float32 pitch_ = 1.0f;
    bool looping_ = false;

    Math::Vector3 position_;
    Math::Vector3 velocity_;
};

/**
 * Audio listener - the "ears"; usually follows the camera
 */
class AudioListener {
public:
    void SetPosition(const Math::Vector3& position);
    Math::Vector3 GetPosition() const { return position_; }

    void SetVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetVelocity() const { return velocity_; }

    void SetOrientation(const Math::Vector3& forward, const Math::Vector3& up);
    Math::Vector3 GetForward() const { return forward_; }
    Math::Vector3 GetUp() const { return up_; }

    void SetGain(float32 gain);
    float32 GetGain() const { return gain_; }

    // Pushes all values to OpenAL (after the context was created)
    void Apply();

private:
    Math::Vector3 position_;
    Math::Vector3 velocity_;
    Math::Vector3 forward_ = {0, 0, -1};
    Math::Vector3 up_ = {0, 1, 0};
    float32 gain_ = 1.0f;
};

/**
 * Streaming source for long OGG files (music, ambience)
 */
class StreamingAudioSource {
public:
    static constexpr uint32 NUM_BUFFERS = 4;
    static constexpr uint32 BUFFER_SAMPLES = 32768;

    StreamingAudioSource() = default;
    ~StreamingAudioSource();

    StreamingAudioSource(const StreamingAudioSource&) = delete;
    StreamingAudioSource& operator=(const StreamingAudioSource&) = delete;

    bool Create();
    void Destroy();

    bool OpenFile(const String& path);
    void Close();

    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const;

    void Update();  // refills processed buffers; called by AudioSystem::Update

    void SetChannel(const String& channel);
    void SetVolume(float32 volume);
    void SetLooping(bool loop);
    void ApplyGain();

private:
    bool FillBuffer(uint32 buffer);
    void UnqueueAll();

    uint32 source_ = 0;
    uint32 buffers_[NUM_BUFFERS] = {};
    void* decoder_ = nullptr;
    Array<int16> scratch_;

    String channel_ = "Music";
    bool looping_ = false;
    float32 volume_ = 1.0f;
    bool playing_ = false;
};

struct MixerChannel {
    float32 volume = 1.0f;
    bool muted = false;
};

/**
 * Audio System
 */
class AudioSystem {
public:
    static AudioSystem& Instance();

    // Returns false when no audio device is available; the system then stays silent
    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);
    bool IsInitialized() const { return initialized_; }

    AudioListener& GetListener() { return listener_; }

    // Clips (cached by name)
    SharedPtr<AudioClip> LoadClip(const String& path);
    SharedPtr<AudioClip> CreateClip(const String& name, const PcmData& pcm);
    SharedPtr<AudioClip> GetClip(const String& name);
    void UnloadClip(const String& name);
    void UnloadAllClips();

    // Persistent sources
    SharedPtr<AudioSource> CreateSource(const String& channel = "SFX");
    void DestroySource(const SharedPtr<AudioSource>& source);
    SharedPtr<StreamingAudioSource> CreateStreamingSource(const String& channel = "Music");
    void DestroyStreamingSource(const SharedPtr<StreamingAudioSource>& source);

    // Fire and forget
    void PlayOneShot(const SharedPtr<AudioClip>& clip, const Math::Vector3& position, float32 volume = 1.0f,
                     float32 pitch = 1.0f, const String& channel = "SFX");
    void PlayOneShot2D(const SharedPtr<AudioClip>& clip, float32 volume = 1.0f, float32 pitch = 1.0f,
                       const String& channel = "SFX");
    uint32 GetActiveOneShotCount() const { return static_cast<uint32>(oneshot_sources_.size()); }

    // Mixer
    void CreateChannel(const String& name);
    void SetChannelVolume(const String& name, float32 volume);
    void SetChannelMuted(const String& name, bool muted);
    float32 GetChannelVolume(const String& name) const;
    // Channel volume x Master channel volume (0 when muted)
    float32 GetChannelGain(const String& name) const;

    // Master volume (listener gain)
    void SetMasterVolume(float32 volume);
    float32 GetMasterVolume() const { return master_volume_; }

    void SetDistanceModel(AttenuationModel model);
    void SetDopplerFactor(float32 factor);
    void SetSpeedOfSound(float32 speed);

    void PauseAll();
    void ResumeAll();
    void StopAll();

    static constexpr uint32 kMaxOneShots = 48;

private:
    AudioSystem() = default;
    ~AudioSystem() = default;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void RefreshChannelGains();
    void CleanupFinishedSources();
    SharedPtr<AudioSource> AcquireOneShot(const SharedPtr<AudioClip>& clip, float32 volume, float32 pitch,
                                          const String& channel);

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
