/**
 * DRX Engine - Audio System Implementation
 * OpenAL-based 3D audio
 */

#include "Audio/Public/AudioSystem.h"
#include "Core/Public/Log.h"

#include <AL/al.h>
#include <AL/alc.h>

// For WAV loading
#include <fstream>
#include <cstring>

// For OGG loading (stb_vorbis)
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace DRX::Audio {

// ============================================================================
// AudioClip Implementation
// ============================================================================

AudioClip::~AudioClip() {
    Unload();
}

bool AudioClip::LoadFromFile(const String& path) {
    name_ = path;

    // Determine format from extension
    size_t dot_pos = path.rfind('.');
    if (dot_pos == String::npos) {
        DRX_LOG_ERROR("Unknown audio format: {}", path);
        return false;
    }

    String ext = path.substr(dot_pos + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));

    if (ext == "wav") {
        return LoadWAV(path);
    } else if (ext == "ogg") {
        return LoadOGG(path);
    }

    DRX_LOG_ERROR("Unsupported audio format: {}", ext);
    return false;
}

bool AudioClip::LoadFromMemory(const void* data, uint32 size, AudioFormat format) {
    // Implementation for loading from memory
    return false;
}

void AudioClip::Unload() {
    if (buffer_ != 0) {
        alDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
}

bool AudioClip::LoadWAV(const String& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DRX_LOG_ERROR("Failed to open WAV file: {}", path);
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        DRX_LOG_ERROR("Invalid WAV file (no RIFF header): {}", path);
        return false;
    }

    uint32 file_size;
    file.read(reinterpret_cast<char*>(&file_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        DRX_LOG_ERROR("Invalid WAV file (no WAVE header): {}", path);
        return false;
    }

    // Find fmt chunk
    char chunk_id[4];
    uint32 chunk_size;

    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16 audio_format;
            file.read(reinterpret_cast<char*>(&audio_format), 2);

            uint16 num_channels;
            file.read(reinterpret_cast<char*>(&num_channels), 2);
            channels_ = num_channels;

            file.read(reinterpret_cast<char*>(&sample_rate_), 4);

            uint32 byte_rate;
            file.read(reinterpret_cast<char*>(&byte_rate), 4);

            uint16 block_align;
            file.read(reinterpret_cast<char*>(&block_align), 2);

            uint16 bits_per_sample;
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

            // Skip extra bytes
            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            Array<char> data(chunk_size);
            file.read(data.data(), chunk_size);

            // Determine OpenAL format
            ALenum format;
            uint16 bits = (chunk_size * 8) / (sample_rate_ * channels_);
            if (channels_ == 1) {
                format = (bits == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
            } else {
                format = (bits == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
            }

            // Create OpenAL buffer
            alGenBuffers(1, &buffer_);
            alBufferData(buffer_, format, data.data(), chunk_size, sample_rate_);

            duration_ = static_cast<float32>(chunk_size) / (sample_rate_ * channels_ * (bits / 8));

            DRX_LOG_INFO("Loaded WAV: {} ({}Hz, {} channels, {:.2f}s)", path, sample_rate_, channels_, duration_);
            return true;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    DRX_LOG_ERROR("Invalid WAV file (no data chunk): {}", path);
    return false;
}

bool AudioClip::LoadOGG(const String& path) {
    int channels, sample_rate;
    short* output;

    int samples = stb_vorbis_decode_filename(path.c_str(), &channels, &sample_rate, &output);
    if (samples <= 0) {
        DRX_LOG_ERROR("Failed to decode OGG file: {}", path);
        return false;
    }

    channels_ = channels;
    sample_rate_ = sample_rate;
    duration_ = static_cast<float32>(samples) / sample_rate_;

    ALenum format = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    alGenBuffers(1, &buffer_);
    alBufferData(buffer_, format, output, samples * channels * sizeof(short), sample_rate);

    free(output);

    DRX_LOG_INFO("Loaded OGG: {} ({}Hz, {} channels, {:.2f}s)", path, sample_rate_, channels_, duration_);
    return true;
}

// ============================================================================
// AudioSource Implementation
// ============================================================================

AudioSource::~AudioSource() {
    Destroy();
}

bool AudioSource::Create() {
    alGenSources(1, &source_);
    if (alGetError() != AL_NO_ERROR) {
        return false;
    }

    alSourcef(source_, AL_PITCH, pitch_);
    alSourcef(source_, AL_GAIN, volume_);
    alSourcei(source_, AL_LOOPING, looping_ ? AL_TRUE : AL_FALSE);

    return true;
}

void AudioSource::Destroy() {
    if (source_ != 0) {
        alDeleteSources(1, &source_);
        source_ = 0;
    }
}

void AudioSource::Play(SharedPtr<AudioClip> clip) {
    if (!source_ || !clip || !clip->IsLoaded()) return;

    current_clip_ = clip;
    alSourcei(source_, AL_BUFFER, clip->GetBuffer());
    alSourcePlay(source_);
}

void AudioSource::Play() {
    if (!source_) return;
    alSourcePlay(source_);
}

void AudioSource::Pause() {
    if (!source_) return;
    alSourcePause(source_);
}

void AudioSource::Stop() {
    if (!source_) return;
    alSourceStop(source_);
}

bool AudioSource::IsPlaying() const {
    if (!source_) return false;

    ALint state;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioSource::IsPaused() const {
    if (!source_) return false;

    ALint state;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

void AudioSource::SetVolume(float32 volume) {
    volume_ = volume;
    if (source_) {
        alSourcef(source_, AL_GAIN, volume_);
    }
}

void AudioSource::SetPitch(float32 pitch) {
    pitch_ = pitch;
    if (source_) {
        alSourcef(source_, AL_PITCH, pitch_);
    }
}

void AudioSource::SetLooping(bool loop) {
    looping_ = loop;
    if (source_) {
        alSourcei(source_, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    }
}

void AudioSource::SetPosition(const Math::Vector3& position) {
    position_ = position;
    if (source_) {
        alSource3f(source_, AL_POSITION, position.x, position.y, position.z);
    }
}

void AudioSource::SetVelocity(const Math::Vector3& velocity) {
    velocity_ = velocity;
    if (source_) {
        alSource3f(source_, AL_VELOCITY, velocity.x, velocity.y, velocity.z);
    }
}

void AudioSource::SetDirection(const Math::Vector3& direction) {
    if (source_) {
        alSource3f(source_, AL_DIRECTION, direction.x, direction.y, direction.z);
    }
}

void AudioSource::SetMinDistance(float32 distance) {
    if (source_) {
        alSourcef(source_, AL_REFERENCE_DISTANCE, distance);
    }
}

void AudioSource::SetMaxDistance(float32 distance) {
    if (source_) {
        alSourcef(source_, AL_MAX_DISTANCE, distance);
    }
}

void AudioSource::SetRolloffFactor(float32 factor) {
    if (source_) {
        alSourcef(source_, AL_ROLLOFF_FACTOR, factor);
    }
}

void AudioSource::SetConeAngles(float32 inner_degrees, float32 outer_degrees) {
    if (source_) {
        alSourcef(source_, AL_CONE_INNER_ANGLE, inner_degrees);
        alSourcef(source_, AL_CONE_OUTER_ANGLE, outer_degrees);
    }
}

void AudioSource::SetConeOuterGain(float32 gain) {
    if (source_) {
        alSourcef(source_, AL_CONE_OUTER_GAIN, gain);
    }
}

float32 AudioSource::GetPlaybackPosition() const {
    if (!source_) return 0.0f;

    float32 pos;
    alGetSourcef(source_, AL_SEC_OFFSET, &pos);
    return pos;
}

void AudioSource::SetPlaybackPosition(float32 seconds) {
    if (source_) {
        alSourcef(source_, AL_SEC_OFFSET, seconds);
    }
}

// ============================================================================
// AudioListener Implementation
// ============================================================================

void AudioListener::SetPosition(const Math::Vector3& position) {
    position_ = position;
    alListener3f(AL_POSITION, position.x, position.y, position.z);
}

void AudioListener::SetVelocity(const Math::Vector3& velocity) {
    velocity_ = velocity;
    alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
}

void AudioListener::SetOrientation(const Math::Vector3& forward, const Math::Vector3& up) {
    forward_ = forward;
    up_ = up;

    float orientation[6] = {
        forward.x, forward.y, forward.z,
        up.x, up.y, up.z
    };
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioListener::SetGain(float32 gain) {
    gain_ = gain;
    alListenerf(AL_GAIN, gain);
}

// ============================================================================
// StreamingAudioSource Implementation
// ============================================================================

StreamingAudioSource::~StreamingAudioSource() {
    Destroy();
}

bool StreamingAudioSource::Create() {
    alGenSources(1, &source_);
    alGenBuffers(NUM_BUFFERS, buffers_);
    return alGetError() == AL_NO_ERROR;
}

void StreamingAudioSource::Destroy() {
    Close();

    if (source_ != 0) {
        alDeleteSources(1, &source_);
        source_ = 0;
    }

    alDeleteBuffers(NUM_BUFFERS, buffers_);
    for (uint32 i = 0; i < NUM_BUFFERS; i++) {
        buffers_[i] = 0;
    }
}

bool StreamingAudioSource::OpenFile(const String& path) {
    file_path_ = path;

    // Open with stb_vorbis for streaming
    int error;
    decoder_ = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
    if (!decoder_) {
        DRX_LOG_ERROR("Failed to open audio file for streaming: {}", path);
        return false;
    }

    // Fill initial buffers
    for (uint32 i = 0; i < NUM_BUFFERS; i++) {
        if (!FillBuffer(buffers_[i])) {
            break;
        }
    }

    alSourceQueueBuffers(source_, NUM_BUFFERS, buffers_);

    return true;
}

void StreamingAudioSource::Close() {
    Stop();

    if (decoder_) {
        stb_vorbis_close(static_cast<stb_vorbis*>(decoder_));
        decoder_ = nullptr;
    }
}

void StreamingAudioSource::Play() {
    if (source_) {
        alSourcePlay(source_);
        playing_ = true;
    }
}

void StreamingAudioSource::Pause() {
    if (source_) {
        alSourcePause(source_);
        playing_ = false;
    }
}

void StreamingAudioSource::Stop() {
    if (source_) {
        alSourceStop(source_);

        ALint queued;
        alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);

        while (queued--) {
            ALuint buffer;
            alSourceUnqueueBuffers(source_, 1, &buffer);
        }

        playing_ = false;
    }
}

bool StreamingAudioSource::IsPlaying() const {
    if (!source_) return false;

    ALint state;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void StreamingAudioSource::Update() {
    if (!playing_ || !decoder_) return;

    ALint processed;
    alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);

    while (processed--) {
        ALuint buffer;
        alSourceUnqueueBuffers(source_, 1, &buffer);

        if (FillBuffer(buffer)) {
            alSourceQueueBuffers(source_, 1, &buffer);
        } else if (looping_) {
            // Seek to beginning
            stb_vorbis_seek_start(static_cast<stb_vorbis*>(decoder_));
            if (FillBuffer(buffer)) {
                alSourceQueueBuffers(source_, 1, &buffer);
            }
        }
    }

    // Restart if stopped but should be playing
    if (playing_ && !IsPlaying()) {
        ALint queued;
        alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0) {
            alSourcePlay(source_);
        } else {
            playing_ = false;
        }
    }
}

void StreamingAudioSource::SetVolume(float32 volume) {
    volume_ = volume;
    if (source_) {
        alSourcef(source_, AL_GAIN, volume);
    }
}

void StreamingAudioSource::SetLooping(bool loop) {
    looping_ = loop;
}

bool StreamingAudioSource::FillBuffer(uint32 buffer) {
    if (!decoder_) return false;

    auto* vorbis = static_cast<stb_vorbis*>(decoder_);
    stb_vorbis_info info = stb_vorbis_get_info(vorbis);

    Array<short> data(BUFFER_SIZE);
    int samples = stb_vorbis_get_samples_short_interleaved(vorbis, info.channels,
                                                           data.data(), BUFFER_SIZE);

    if (samples <= 0) {
        return false;
    }

    ALenum format = (info.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(buffer, format, data.data(), samples * info.channels * sizeof(short), info.sample_rate);

    return true;
}

// ============================================================================
// AudioSystem Implementation
// ============================================================================

AudioSystem& AudioSystem::Instance() {
    static AudioSystem instance;
    return instance;
}

bool AudioSystem::Initialize() {
    // Open default device
    device_ = alcOpenDevice(nullptr);
    if (!device_) {
        DRX_LOG_ERROR("Failed to open audio device");
        return false;
    }

    // Create context
    context_ = alcCreateContext(device_, nullptr);
    if (!context_) {
        DRX_LOG_ERROR("Failed to create audio context");
        alcCloseDevice(device_);
        return false;
    }

    alcMakeContextCurrent(context_);

    // Set default listener
    listener_.SetPosition({0, 0, 0});
    listener_.SetOrientation({0, 0, -1}, {0, 1, 0});

    // Create default mixer channels
    CreateChannel("Master");
    CreateChannel("Music");
    CreateChannel("SFX");
    CreateChannel("Voice");
    CreateChannel("Ambient");

    initialized_ = true;
    DRX_LOG_INFO("Audio system initialized");
    return true;
}

void AudioSystem::Shutdown() {
    StopAll();

    clips_.clear();
    sources_.clear();
    streaming_sources_.clear();
    oneshot_sources_.clear();
    mixer_channels_.clear();

    if (context_) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context_);
        context_ = nullptr;
    }

    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }

    initialized_ = false;
    DRX_LOG_INFO("Audio system shutdown");
}

void AudioSystem::Update(float32 delta_time) {
    // Update streaming sources
    for (auto& source : streaming_sources_) {
        source->Update();
    }

    // Cleanup finished one-shot sources
    CleanupFinishedSources();
}

SharedPtr<AudioClip> AudioSystem::LoadClip(const String& path) {
    auto it = clips_.find(path);
    if (it != clips_.end()) {
        return it->second;
    }

    auto clip = MakeShared<AudioClip>();
    if (!clip->LoadFromFile(path)) {
        return nullptr;
    }

    clips_[path] = clip;
    return clip;
}

SharedPtr<AudioClip> AudioSystem::GetClip(const String& name) {
    auto it = clips_.find(name);
    return it != clips_.end() ? it->second : nullptr;
}

void AudioSystem::UnloadClip(const String& name) {
    clips_.erase(name);
}

void AudioSystem::UnloadAllClips() {
    clips_.clear();
}

SharedPtr<AudioSource> AudioSystem::CreateSource() {
    auto source = MakeShared<AudioSource>();
    if (!source->Create()) {
        return nullptr;
    }
    sources_.push_back(source);
    return source;
}

void AudioSystem::DestroySource(SharedPtr<AudioSource> source) {
    auto it = std::find(sources_.begin(), sources_.end(), source);
    if (it != sources_.end()) {
        sources_.erase(it);
    }
}

SharedPtr<StreamingAudioSource> AudioSystem::CreateStreamingSource() {
    auto source = MakeShared<StreamingAudioSource>();
    if (!source->Create()) {
        return nullptr;
    }
    streaming_sources_.push_back(source);
    return source;
}

void AudioSystem::DestroyStreamingSource(SharedPtr<StreamingAudioSource> source) {
    auto it = std::find(streaming_sources_.begin(), streaming_sources_.end(), source);
    if (it != streaming_sources_.end()) {
        streaming_sources_.erase(it);
    }
}

void AudioSystem::PlayOneShot(SharedPtr<AudioClip> clip, const Math::Vector3& position, float32 volume) {
    if (!clip) return;

    auto source = MakeShared<AudioSource>();
    if (!source->Create()) return;

    source->SetVolume(volume * master_volume_);
    source->SetPosition(position);
    source->Play(clip);

    oneshot_sources_.push_back(source);
}

void AudioSystem::PlayOneShot2D(SharedPtr<AudioClip> clip, float32 volume) {
    if (!clip) return;

    auto source = MakeShared<AudioSource>();
    if (!source->Create()) return;

    source->SetVolume(volume * master_volume_);
    alSourcei(source->GetSource(), AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source->GetSource(), AL_POSITION, 0, 0, 0);
    source->Play(clip);

    oneshot_sources_.push_back(source);
}

void AudioSystem::CreateChannel(const String& name) {
    MixerChannel channel;
    channel.name = name;
    mixer_channels_[name] = channel;
}

void AudioSystem::SetChannelVolume(const String& name, float32 volume) {
    auto it = mixer_channels_.find(name);
    if (it != mixer_channels_.end()) {
        it->second.volume = volume;
    }
}

void AudioSystem::SetChannelMuted(const String& name, bool muted) {
    auto it = mixer_channels_.find(name);
    if (it != mixer_channels_.end()) {
        it->second.muted = muted;
    }
}

float32 AudioSystem::GetChannelVolume(const String& name) const {
    auto it = mixer_channels_.find(name);
    return it != mixer_channels_.end() ? it->second.volume : 1.0f;
}

void AudioSystem::SetMasterVolume(float32 volume) {
    master_volume_ = volume;
    listener_.SetGain(volume);
}

void AudioSystem::SetDistanceModel(AttenuationModel model) {
    ALenum al_model;
    switch (model) {
        case AttenuationModel::None:
            al_model = AL_NONE;
            break;
        case AttenuationModel::Inverse:
            al_model = AL_INVERSE_DISTANCE;
            break;
        case AttenuationModel::InverseClamped:
            al_model = AL_INVERSE_DISTANCE_CLAMPED;
            break;
        case AttenuationModel::Linear:
            al_model = AL_LINEAR_DISTANCE;
            break;
        case AttenuationModel::LinearClamped:
            al_model = AL_LINEAR_DISTANCE_CLAMPED;
            break;
        case AttenuationModel::Exponential:
            al_model = AL_EXPONENT_DISTANCE;
            break;
        case AttenuationModel::ExponentialClamped:
            al_model = AL_EXPONENT_DISTANCE_CLAMPED;
            break;
        default:
            al_model = AL_INVERSE_DISTANCE_CLAMPED;
    }
    alDistanceModel(al_model);
}

void AudioSystem::SetDopplerFactor(float32 factor) {
    alDopplerFactor(factor);
}

void AudioSystem::SetSpeedOfSound(float32 speed) {
    alSpeedOfSound(speed);
}

void AudioSystem::PauseAll() {
    for (auto& source : sources_) {
        source->Pause();
    }
    for (auto& source : streaming_sources_) {
        source->Pause();
    }
}

void AudioSystem::ResumeAll() {
    for (auto& source : sources_) {
        if (source->IsPaused()) {
            source->Play();
        }
    }
    for (auto& source : streaming_sources_) {
        source->Play();
    }
}

void AudioSystem::StopAll() {
    for (auto& source : sources_) {
        source->Stop();
    }
    for (auto& source : streaming_sources_) {
        source->Stop();
    }
    oneshot_sources_.clear();
}

void AudioSystem::CleanupFinishedSources() {
    oneshot_sources_.erase(
        std::remove_if(oneshot_sources_.begin(), oneshot_sources_.end(),
            [](const SharedPtr<AudioSource>& source) {
                return !source->IsPlaying();
            }),
        oneshot_sources_.end()
    );
}

} // namespace DRX::Audio
