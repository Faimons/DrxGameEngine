/**
 * DRX Engine - Audio System Implementation
 */

#include "Audio/Public/AudioSystem.h"
#include "Core/Public/Log.h"

#include <AL/al.h>
#include <AL/alc.h>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace DRX::Audio {

namespace {

bool HasContext() {
    return alcGetCurrentContext() != nullptr;
}

ALenum PcmFormat(uint32 channels) {
    return channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
}

uint16 ReadU16(const uint8* p) { return static_cast<uint16>(p[0] | (p[1] << 8)); }
uint32 ReadU32(const uint8* p) {
    return static_cast<uint32>(p[0]) | (static_cast<uint32>(p[1]) << 8) | (static_cast<uint32>(p[2]) << 16) |
           (static_cast<uint32>(p[3]) << 24);
}

int16 FloatToPcm(float32 v) {
    return static_cast<int16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}

bool DecodeWav(const uint8* data, size_t size, PcmData& out, String& error) {
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }

    uint16 format_tag = 0, channels = 0, bits = 0, block_align = 0;
    uint32 sample_rate = 0;
    const uint8* samples = nullptr;
    size_t samples_size = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8* chunk = data + pos;
        const uint32 chunk_size = ReadU32(chunk + 4);
        const size_t body = pos + 8;
        const size_t available = size - body;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16 || available < 16) {
                error = "truncated fmt chunk";
                return false;
            }
            format_tag = ReadU16(data + body);
            channels = ReadU16(data + body + 2);
            sample_rate = ReadU32(data + body + 4);
            block_align = ReadU16(data + body + 12);
            bits = ReadU16(data + body + 14);
            if (format_tag == 0xFFFE && chunk_size >= 26 && available >= 26) {
                format_tag = ReadU16(data + body + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            samples = data + body;
            samples_size = std::min<size_t>(chunk_size, available);   // tolerate truncated files
        }
        pos = body + chunk_size + (chunk_size & 1u);   // chunks are word aligned
    }

    if (!samples) {
        error = "no data chunk";
        return false;
    }
    if (channels == 0 || channels > 2) {
        error = fmt::format("{} channels (mono or stereo supported)", channels);
        return false;
    }
    if (sample_rate == 0 || block_align == 0) {
        error = "invalid fmt chunk";
        return false;
    }

    const bool is_float = format_tag == 3;
    if (!(format_tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(is_float && bits == 32)) {
        error = fmt::format("unsupported sample format (tag {}, {} bits)", format_tag, bits);
        return false;
    }

    const uint32 bytes_per_sample = bits / 8;
    const size_t sample_count = samples_size / bytes_per_sample;
    out.channels = channels;
    out.sample_rate = sample_rate;
    out.samples.resize(sample_count - sample_count % channels);

    for (size_t i = 0; i < out.samples.size(); ++i) {
        const uint8* s = samples + i * bytes_per_sample;
        int16 value = 0;
        if (is_float) {
            float32 f;
            std::memcpy(&f, s, sizeof(f));
            value = FloatToPcm(f);
        } else if (bits == 8) {
            value = static_cast<int16>((static_cast<int32>(s[0]) - 128) << 8);
        } else if (bits == 16) {
            value = static_cast<int16>(ReadU16(s));
        } else if (bits == 24) {
            value = static_cast<int16>(static_cast<int32>(s[1] | (s[2] << 8)));
        } else {
            value = static_cast<int16>(static_cast<int32>(ReadU32(s)) >> 16);
        }
        out.samples[i] = value;
    }
    return true;
}

bool DecodeOgg(const uint8* data, size_t size, PcmData& out, String& error) {
    int channels = 0, sample_rate = 0;
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_memory(data, static_cast<int>(size), &channels, &sample_rate, &decoded);
    if (frames <= 0 || !decoded) {
        error = "invalid Ogg Vorbis data";
        return false;
    }
    if (channels < 1 || channels > 2) {
        std::free(decoded);
        error = fmt::format("{} channels (mono or stereo supported)", channels);
        return false;
    }
    out.channels = static_cast<uint32>(channels);
    out.sample_rate = static_cast<uint32>(sample_rate);
    out.samples.assign(decoded, decoded + static_cast<size_t>(frames) * channels);
    std::free(decoded);
    return true;
}

} // namespace

AudioFormat DetectAudioFormat(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8*>(data);
    if (size >= 12 && std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WAVE", 4) == 0) {
        return AudioFormat::WAV;
    }
    if (size >= 4 && std::memcmp(bytes, "OggS", 4) == 0) {
        return AudioFormat::OGG;
    }
    return AudioFormat::Unknown;
}

bool DecodeAudio(const void* data, size_t size, AudioFormat format, PcmData& out, String& error) {
    if (!data || size == 0) {
        error = "empty input";
        return false;
    }
    if (format == AudioFormat::Unknown) {
        format = DetectAudioFormat(data, size);
    }
    PcmData pcm;
    bool ok = false;
    switch (format) {
        case AudioFormat::WAV: ok = DecodeWav(static_cast<const uint8*>(data), size, pcm, error); break;
        case AudioFormat::OGG: ok = DecodeOgg(static_cast<const uint8*>(data), size, pcm, error); break;
        default: error = "unknown audio format"; break;
    }
    if (ok && pcm.samples.empty()) {
        error = "no samples";
        ok = false;
    }
    if (ok) out = std::move(pcm);
    return ok;
}

// ============================================================================
// AudioClip
// ============================================================================

AudioClip::~AudioClip() {
    Unload();
}

bool AudioClip::LoadFromFile(const String& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DRX_LOG_ERROR("Audio file not found: {}", path);
        return false;
    }
    const Array<uint8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return LoadFromMemory(bytes.data(), bytes.size(), AudioFormat::Unknown, path);
}

bool AudioClip::LoadFromMemory(const void* data, size_t size, AudioFormat format, const String& name) {
    PcmData pcm;
    String error;
    if (!DecodeAudio(data, size, format, pcm, error)) {
        DRX_LOG_ERROR("Cannot decode audio '{}': {}", name.empty() ? String("<memory>") : name, error);
        return false;
    }
    return LoadFromPCM(pcm, name);
}

bool AudioClip::LoadFromPCM(const PcmData& pcm, const String& name) {
    if (pcm.channels < 1 || pcm.channels > 2 || pcm.sample_rate == 0 || pcm.samples.empty()) {
        DRX_LOG_ERROR("Invalid PCM data for clip '{}'", name);
        return false;
    }
    if (!HasContext()) {
        DRX_LOG_WARNING("Audio clip '{}' not created: audio system is not initialized", name);
        return false;
    }

    Unload();
    alGetError();
    alGenBuffers(1, &buffer_);
    alBufferData(buffer_, PcmFormat(pcm.channels), pcm.samples.data(),
                 static_cast<ALsizei>(pcm.samples.size() * sizeof(int16)), static_cast<ALsizei>(pcm.sample_rate));
    if (alGetError() != AL_NO_ERROR) {
        DRX_LOG_ERROR("OpenAL rejected audio clip '{}'", name);
        Unload();
        return false;
    }

    name_ = name;
    channels_ = pcm.channels;
    sample_rate_ = pcm.sample_rate;
    duration_ = pcm.GetDuration();
    return true;
}

void AudioClip::Unload() {
    if (buffer_ != 0 && HasContext()) {
        alDeleteBuffers(1, &buffer_);
    }
    buffer_ = 0;
}

// ============================================================================
// AudioSource
// ============================================================================

AudioSource::~AudioSource() {
    Destroy();
}

bool AudioSource::Create() {
    if (source_ != 0) return true;
    if (!HasContext()) return false;

    alGetError();
    alGenSources(1, &source_);
    if (alGetError() != AL_NO_ERROR) {
        source_ = 0;
        return false;
    }
    alSourcef(source_, AL_PITCH, pitch_);
    alSourcei(source_, AL_LOOPING, looping_ ? AL_TRUE : AL_FALSE);
    ApplyGain();
    return true;
}

void AudioSource::Destroy() {
    if (source_ != 0 && HasContext()) {
        alSourceStop(source_);
        alDeleteSources(1, &source_);
    }
    source_ = 0;
    current_clip_.reset();
}

void AudioSource::Play(SharedPtr<AudioClip> clip) {
    if (!source_ || !clip || !clip->IsLoaded()) return;
    alSourceStop(source_);
    current_clip_ = std::move(clip);
    alSourcei(source_, AL_BUFFER, static_cast<ALint>(current_clip_->GetBuffer()));
    alSourcePlay(source_);
}

void AudioSource::Play() {
    if (source_) alSourcePlay(source_);
}

void AudioSource::Pause() {
    if (source_) alSourcePause(source_);
}

void AudioSource::Stop() {
    if (source_) alSourceStop(source_);
}

bool AudioSource::IsPlaying() const {
    if (!source_) return false;
    ALint state = AL_STOPPED;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioSource::IsPaused() const {
    if (!source_) return false;
    ALint state = AL_STOPPED;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

void AudioSource::SetChannel(const String& channel) {
    channel_ = channel;
    ApplyGain();
}

void AudioSource::ApplyGain() {
    if (source_) {
        alSourcef(source_, AL_GAIN, std::max(0.0f, volume_) * AudioSystem::Instance().GetChannelGain(channel_));
    }
}

void AudioSource::SetVolume(float32 volume) {
    volume_ = volume;
    ApplyGain();
}

void AudioSource::SetPitch(float32 pitch) {
    pitch_ = std::max(0.01f, pitch);
    if (source_) alSourcef(source_, AL_PITCH, pitch_);
}

void AudioSource::SetLooping(bool loop) {
    looping_ = loop;
    if (source_) alSourcei(source_, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

void AudioSource::SetRelative(bool relative) {
    if (source_) alSourcei(source_, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
}

void AudioSource::SetPosition(const Math::Vector3& position) {
    position_ = position;
    if (source_) alSource3f(source_, AL_POSITION, position.x, position.y, position.z);
}

void AudioSource::SetVelocity(const Math::Vector3& velocity) {
    velocity_ = velocity;
    if (source_) alSource3f(source_, AL_VELOCITY, velocity.x, velocity.y, velocity.z);
}

void AudioSource::SetDirection(const Math::Vector3& direction) {
    if (source_) alSource3f(source_, AL_DIRECTION, direction.x, direction.y, direction.z);
}

void AudioSource::SetMinDistance(float32 distance) {
    if (source_) alSourcef(source_, AL_REFERENCE_DISTANCE, distance);
}

void AudioSource::SetMaxDistance(float32 distance) {
    if (source_) alSourcef(source_, AL_MAX_DISTANCE, distance);
}

void AudioSource::SetRolloffFactor(float32 factor) {
    if (source_) alSourcef(source_, AL_ROLLOFF_FACTOR, factor);
}

void AudioSource::SetConeAngles(float32 inner_degrees, float32 outer_degrees) {
    if (!source_) return;
    alSourcef(source_, AL_CONE_INNER_ANGLE, inner_degrees);
    alSourcef(source_, AL_CONE_OUTER_ANGLE, outer_degrees);
}

void AudioSource::SetConeOuterGain(float32 gain) {
    if (source_) alSourcef(source_, AL_CONE_OUTER_GAIN, gain);
}

float32 AudioSource::GetPlaybackPosition() const {
    if (!source_) return 0.0f;
    ALfloat seconds = 0.0f;
    alGetSourcef(source_, AL_SEC_OFFSET, &seconds);
    return seconds;
}

void AudioSource::SetPlaybackPosition(float32 seconds) {
    if (source_) alSourcef(source_, AL_SEC_OFFSET, seconds);
}

// ============================================================================
// AudioListener
// ============================================================================

void AudioListener::SetPosition(const Math::Vector3& position) {
    position_ = position;
    if (HasContext()) alListener3f(AL_POSITION, position.x, position.y, position.z);
}

void AudioListener::SetVelocity(const Math::Vector3& velocity) {
    velocity_ = velocity;
    if (HasContext()) alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
}

void AudioListener::SetOrientation(const Math::Vector3& forward, const Math::Vector3& up) {
    forward_ = forward;
    up_ = up;
    if (HasContext()) {
        const ALfloat orientation[6] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
        alListenerfv(AL_ORIENTATION, orientation);
    }
}

void AudioListener::SetGain(float32 gain) {
    gain_ = std::max(0.0f, gain);
    if (HasContext()) alListenerf(AL_GAIN, gain_);
}

void AudioListener::Apply() {
    SetPosition(position_);
    SetVelocity(velocity_);
    SetOrientation(forward_, up_);
    SetGain(gain_);
}

// ============================================================================
// StreamingAudioSource
// ============================================================================

StreamingAudioSource::~StreamingAudioSource() {
    Destroy();
}

bool StreamingAudioSource::Create() {
    if (source_ != 0) return true;
    if (!HasContext()) return false;

    alGetError();
    alGenSources(1, &source_);
    alGenBuffers(NUM_BUFFERS, buffers_);
    if (alGetError() != AL_NO_ERROR) {
        Destroy();
        return false;
    }
    ApplyGain();
    return true;
}

void StreamingAudioSource::Destroy() {
    Close();
    if (HasContext()) {
        if (source_ != 0) alDeleteSources(1, &source_);
        for (uint32& buffer : buffers_) {
            if (buffer != 0) alDeleteBuffers(1, &buffer);
        }
    }
    source_ = 0;
    std::fill(std::begin(buffers_), std::end(buffers_), 0u);
}

bool StreamingAudioSource::OpenFile(const String& path) {
    if (!source_) return false;
    Close();

    int error = 0;
    auto* vorbis = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
    if (!vorbis) {
        DRX_LOG_ERROR("Cannot open audio stream {} (error {})", path, error);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels < 1 || info.channels > 2) {
        DRX_LOG_ERROR("Audio stream {} has {} channels (mono or stereo supported)", path, info.channels);
        stb_vorbis_close(vorbis);
        return false;
    }
    decoder_ = vorbis;

    ALsizei queued = 0;
    for (uint32 buffer : buffers_) {
        if (!FillBuffer(buffer)) break;
        ++queued;
    }
    if (queued > 0) alSourceQueueBuffers(source_, queued, buffers_);
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
    if (!source_ || !decoder_) return;

    ALint queued = 0;
    alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
    if (queued == 0) {
        // Stopped (or finished): restart from the beginning
        stb_vorbis_seek_start(static_cast<stb_vorbis*>(decoder_));
        for (uint32 buffer : buffers_) {
            if (!FillBuffer(buffer)) break;
            alSourceQueueBuffers(source_, 1, &buffer);
            ++queued;
        }
    }
    if (queued > 0) {
        alSourcePlay(source_);
        playing_ = true;
    }
}

void StreamingAudioSource::Pause() {
    if (source_) alSourcePause(source_);
    playing_ = false;
}

void StreamingAudioSource::Stop() {
    if (source_) {
        alSourceStop(source_);
        UnqueueAll();
    }
    playing_ = false;
}

void StreamingAudioSource::UnqueueAll() {
    ALint queued = 0;
    alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(source_, 1, &buffer);
    }
}

bool StreamingAudioSource::IsPlaying() const {
    if (!source_) return false;
    ALint state = AL_STOPPED;
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void StreamingAudioSource::Update() {
    if (!playing_ || !decoder_ || !source_) return;

    ALint processed = 0;
    alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(source_, 1, &buffer);
        bool filled = FillBuffer(buffer);
        if (!filled && looping_) {
            stb_vorbis_seek_start(static_cast<stb_vorbis*>(decoder_));
            filled = FillBuffer(buffer);
        }
        if (filled) alSourceQueueBuffers(source_, 1, &buffer);
    }

    // A buffer underrun stops the source; resume while data is queued
    if (!IsPlaying()) {
        ALint queued = 0;
        alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0) alSourcePlay(source_);
        else playing_ = false;
    }
}

void StreamingAudioSource::SetChannel(const String& channel) {
    channel_ = channel;
    ApplyGain();
}

void StreamingAudioSource::SetVolume(float32 volume) {
    volume_ = volume;
    ApplyGain();
}

void StreamingAudioSource::ApplyGain() {
    if (source_) {
        alSourcef(source_, AL_GAIN, std::max(0.0f, volume_) * AudioSystem::Instance().GetChannelGain(channel_));
    }
}

void StreamingAudioSource::SetLooping(bool loop) {
    looping_ = loop;
}

bool StreamingAudioSource::FillBuffer(uint32 buffer) {
    auto* vorbis = static_cast<stb_vorbis*>(decoder_);
    if (!vorbis) return false;
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    const auto channels = static_cast<uint32>(info.channels);

    scratch_.resize(static_cast<size_t>(BUFFER_SAMPLES) * channels);
    const int frames = stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, scratch_.data(),
                                                                static_cast<int>(scratch_.size()));
    if (frames <= 0) return false;

    alBufferData(buffer, PcmFormat(channels), scratch_.data(),
                 static_cast<ALsizei>(static_cast<size_t>(frames) * channels * sizeof(int16)),
                 static_cast<ALsizei>(info.sample_rate));
    return true;
}

// ============================================================================
// AudioSystem
// ============================================================================

AudioSystem& AudioSystem::Instance() {
    static AudioSystem instance;
    return instance;
}

bool AudioSystem::Initialize() {
    if (initialized_) return true;

    for (const char* channel : {"Master", "Music", "SFX", "Voice", "Ambient"}) {
        CreateChannel(channel);
    }

    device_ = alcOpenDevice(nullptr);
    if (!device_) {
        DRX_LOG_WARNING("No audio device available");
        return false;
    }
    context_ = alcCreateContext(device_, nullptr);
    if (!context_ || !alcMakeContextCurrent(context_)) {
        DRX_LOG_ERROR("Failed to create audio context");
        if (context_) alcDestroyContext(context_);
        alcCloseDevice(device_);
        context_ = nullptr;
        device_ = nullptr;
        return false;
    }

    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    listener_.SetGain(master_volume_);
    listener_.Apply();

    initialized_ = true;
    const ALCchar* name = alcGetString(device_, ALC_DEVICE_SPECIFIER);
    DRX_LOG_INFO("Audio system initialized ({})", name ? name : "default device");
    return true;
}

void AudioSystem::Shutdown() {
    if (!initialized_) return;

    StopAll();
    oneshot_sources_.clear();
    // Explicitly release OpenAL objects that may still be referenced elsewhere
    for (auto& source : streaming_sources_) source->Destroy();
    for (auto& source : sources_) source->Destroy();
    for (auto& [name, clip] : clips_) clip->Unload();
    streaming_sources_.clear();
    sources_.clear();
    clips_.clear();

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context_);
    alcCloseDevice(device_);
    context_ = nullptr;
    device_ = nullptr;

    initialized_ = false;
    DRX_LOG_INFO("Audio system shutdown");
}

void AudioSystem::Update(float32 /*delta_time*/) {
    if (!initialized_) return;
    for (auto& source : streaming_sources_) {
        source->Update();
    }
    CleanupFinishedSources();
}

SharedPtr<AudioClip> AudioSystem::LoadClip(const String& path) {
    if (auto existing = GetClip(path)) return existing;
    if (!initialized_) return nullptr;

    auto clip = MakeShared<AudioClip>();
    if (!clip->LoadFromFile(path)) return nullptr;
    clips_[path] = clip;
    return clip;
}

SharedPtr<AudioClip> AudioSystem::CreateClip(const String& name, const PcmData& pcm) {
    if (!initialized_) return nullptr;
    auto clip = MakeShared<AudioClip>();
    if (!clip->LoadFromPCM(pcm, name)) return nullptr;
    clips_[name] = clip;
    return clip;
}

SharedPtr<AudioClip> AudioSystem::GetClip(const String& name) {
    const auto it = clips_.find(name);
    return it != clips_.end() ? it->second : nullptr;
}

void AudioSystem::UnloadClip(const String& name) {
    clips_.erase(name);
}

void AudioSystem::UnloadAllClips() {
    clips_.clear();
}

SharedPtr<AudioSource> AudioSystem::CreateSource(const String& channel) {
    if (!initialized_) return nullptr;
    auto source = MakeShared<AudioSource>();
    if (!source->Create()) return nullptr;
    source->SetChannel(channel);
    sources_.push_back(source);
    return source;
}

void AudioSystem::DestroySource(const SharedPtr<AudioSource>& source) {
    sources_.erase(std::remove(sources_.begin(), sources_.end(), source), sources_.end());
}

SharedPtr<StreamingAudioSource> AudioSystem::CreateStreamingSource(const String& channel) {
    if (!initialized_) return nullptr;
    auto source = MakeShared<StreamingAudioSource>();
    if (!source->Create()) return nullptr;
    source->SetChannel(channel);
    streaming_sources_.push_back(source);
    return source;
}

void AudioSystem::DestroyStreamingSource(const SharedPtr<StreamingAudioSource>& source) {
    streaming_sources_.erase(std::remove(streaming_sources_.begin(), streaming_sources_.end(), source),
                             streaming_sources_.end());
}

SharedPtr<AudioSource> AudioSystem::AcquireOneShot(const SharedPtr<AudioClip>& clip, float32 volume, float32 pitch,
                                                   const String& channel) {
    if (!initialized_ || !clip || !clip->IsLoaded()) return nullptr;

    CleanupFinishedSources();
    if (oneshot_sources_.size() >= kMaxOneShots) {
        oneshot_sources_.erase(oneshot_sources_.begin());   // steal the oldest voice
    }

    auto source = MakeShared<AudioSource>();
    if (!source->Create()) return nullptr;
    source->SetChannel(channel);
    source->SetVolume(volume);
    source->SetPitch(pitch);
    oneshot_sources_.push_back(source);
    return source;
}

void AudioSystem::PlayOneShot(const SharedPtr<AudioClip>& clip, const Math::Vector3& position, float32 volume,
                              float32 pitch, const String& channel) {
    if (auto source = AcquireOneShot(clip, volume, pitch, channel)) {
        source->SetPosition(position);
        source->Play(clip);
    }
}

void AudioSystem::PlayOneShot2D(const SharedPtr<AudioClip>& clip, float32 volume, float32 pitch,
                                const String& channel) {
    if (auto source = AcquireOneShot(clip, volume, pitch, channel)) {
        source->SetRelative(true);
        source->SetPosition(Math::Vector3::Zero);
        source->Play(clip);
    }
}

void AudioSystem::CreateChannel(const String& name) {
    mixer_channels_.try_emplace(name);
}

void AudioSystem::SetChannelVolume(const String& name, float32 volume) {
    mixer_channels_[name].volume = std::max(0.0f, volume);
    RefreshChannelGains();
}

void AudioSystem::SetChannelMuted(const String& name, bool muted) {
    mixer_channels_[name].muted = muted;
    RefreshChannelGains();
}

float32 AudioSystem::GetChannelVolume(const String& name) const {
    const auto it = mixer_channels_.find(name);
    return it != mixer_channels_.end() ? it->second.volume : 1.0f;
}

float32 AudioSystem::GetChannelGain(const String& name) const {
    auto gain_of = [this](const String& channel) {
        const auto it = mixer_channels_.find(channel);
        if (it == mixer_channels_.end()) return 1.0f;
        return it->second.muted ? 0.0f : it->second.volume;
    };
    return name == "Master" ? gain_of(name) : gain_of(name) * gain_of("Master");
}

void AudioSystem::RefreshChannelGains() {
    for (auto& source : sources_) source->ApplyGain();
    for (auto& source : oneshot_sources_) source->ApplyGain();
    for (auto& source : streaming_sources_) source->ApplyGain();
}

void AudioSystem::SetMasterVolume(float32 volume) {
    master_volume_ = std::max(0.0f, volume);
    listener_.SetGain(master_volume_);
}

void AudioSystem::SetDistanceModel(AttenuationModel model) {
    if (!initialized_) return;
    ALenum al_model = AL_INVERSE_DISTANCE_CLAMPED;
    switch (model) {
        case AttenuationModel::None: al_model = AL_NONE; break;
        case AttenuationModel::Inverse: al_model = AL_INVERSE_DISTANCE; break;
        case AttenuationModel::InverseClamped: al_model = AL_INVERSE_DISTANCE_CLAMPED; break;
        case AttenuationModel::Linear: al_model = AL_LINEAR_DISTANCE; break;
        case AttenuationModel::LinearClamped: al_model = AL_LINEAR_DISTANCE_CLAMPED; break;
        case AttenuationModel::Exponential: al_model = AL_EXPONENT_DISTANCE; break;
        case AttenuationModel::ExponentialClamped: al_model = AL_EXPONENT_DISTANCE_CLAMPED; break;
    }
    alDistanceModel(al_model);
}

void AudioSystem::SetDopplerFactor(float32 factor) {
    if (initialized_) alDopplerFactor(factor);
}

void AudioSystem::SetSpeedOfSound(float32 speed) {
    if (initialized_) alSpeedOfSound(speed);
}

void AudioSystem::PauseAll() {
    for (auto& source : sources_) source->Pause();
    for (auto& source : oneshot_sources_) source->Pause();
    for (auto& source : streaming_sources_) source->Pause();
}

void AudioSystem::ResumeAll() {
    for (auto& source : sources_) {
        if (source->IsPaused()) source->Play();
    }
    for (auto& source : oneshot_sources_) {
        if (source->IsPaused()) source->Play();
    }
    for (auto& source : streaming_sources_) source->Play();
}

void AudioSystem::StopAll() {
    for (auto& source : sources_) source->Stop();
    for (auto& source : streaming_sources_) source->Stop();
    oneshot_sources_.clear();
}

void AudioSystem::CleanupFinishedSources() {
    oneshot_sources_.erase(std::remove_if(oneshot_sources_.begin(), oneshot_sources_.end(),
                                          [](const SharedPtr<AudioSource>& source) {
                                              return !source->IsPlaying() && !source->IsPaused();
                                          }),
                           oneshot_sources_.end());
}

} // namespace DRX::Audio
