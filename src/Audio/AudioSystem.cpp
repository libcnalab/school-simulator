#include "Audio/AudioSystem.hpp"

#include <algorithm>
#include <exception>

#include "App/Log.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Audio;

AudioSystem::AudioSystem(Content::ContentManager& content, bool enabled, float masterVolume)
    : content_(content), enabled_(enabled), master_(std::clamp(masterVolume, 0.0f, 1.0f))
{
    if (!enabled_) return;
    try {
        // XNA attenuates by 1/distance in "distance scale" units; 4 means full volume within four
        // metres, then a smooth roll-off that Voice3D::radius finally brings to silence.
        SoundEffect::setDistanceScaleProperty(4.0f);
        SoundEffect::setDopplerScaleProperty(0.0f);
        SoundEffect::setMasterVolumeProperty(master_);
        listener_.setUpProperty(Vector3::Up);
        listener_.setForwardProperty(Vector3(0, 0, -1));
    } catch (const std::exception& e) {
        School::Log("audio", "audio unavailable: %s", e.what());
        enabled_ = false;
    }
}

void AudioSystem::SetMasterVolume(float volume)
{
    master_ = std::clamp(volume, 0.0f, 1.0f);
    if (enabled_ && !muted_) SoundEffect::setMasterVolumeProperty(master_);
}

void AudioSystem::SetMuted(bool muted)
{
    muted_ = muted;
    if (enabled_) SoundEffect::setMasterVolumeProperty(muted_ ? 0.0f : master_);
}

SoundEffect* AudioSystem::Effect(const std::string& name)
{
    if (!enabled_) return nullptr;
    auto it = effects_.find(name);
    if (it != effects_.end()) return it->second.get();
    std::unique_ptr<SoundEffect> effect;
    try {
        effect = std::make_unique<SoundEffect>(content_.Load<SoundEffect>("Audio/" + name));
    } catch (const std::exception& e) {
        School::Log("audio", "missing or unreadable sound '%s': %s", name.c_str(), e.what());
    }
    SoundEffect* raw = effect.get();
    effects_[name] = std::move(effect);
    return raw;
}

void AudioSystem::SetListener(const Vector3& position, const Vector3& forward)
{
    if (!enabled_) return;
    listener_.setPositionProperty(position);
    listener_.setForwardProperty(forward);
}

void AudioSystem::PlayUi(const std::string& name, float volume, float pitch)
{
    SoundEffect* effect = Effect(name);
    if (effect == nullptr) return;
    try {
        effect->Play(std::clamp(volume, 0.0f, 1.0f), pitch, 0.0f);
    } catch (const std::exception& e) {
        School::Log("audio", "cannot play '%s': %s", name.c_str(), e.what());
    }
}

void AudioSystem::PlayAt(const std::string& name, const Vector3& position, float volume, float radius)
{
    SoundEffect* effect = Effect(name);
    if (effect == nullptr) return;
    try {
        auto voice = std::make_unique<Voice3D>();
        voice->instance = std::make_unique<SoundEffectInstance>(effect->CreateInstance());
        voice->position = position;
        voice->baseVolume = volume;
        voice->radius = radius;
        voice->emitter.setPositionProperty(position);
        voice->wantPlaying = true;
        ApplyVoice(*voice);
        voice->instance->Play();
        oneShots_.push_back(std::move(voice));
    } catch (const std::exception& e) {
        School::Log("audio", "cannot play '%s': %s", name.c_str(), e.what());
    }
}

Voice3D* AudioSystem::CreateLoop(const std::string& name, const Vector3& position, float baseVolume, float radius)
{
    SoundEffect* effect = Effect(name);
    if (effect == nullptr) return nullptr;
    try {
        auto voice = std::make_unique<Voice3D>();
        voice->instance = std::make_unique<SoundEffectInstance>(effect->CreateInstance());
        voice->instance->setIsLoopedProperty(true);
        voice->position = position;
        voice->baseVolume = baseVolume;
        voice->radius = radius;
        voice->emitter.setPositionProperty(position);
        loops_.push_back(std::move(voice));
        return loops_.back().get();
    } catch (const std::exception& e) {
        School::Log("audio", "cannot create loop '%s': %s", name.c_str(), e.what());
        return nullptr;
    }
}

void AudioSystem::SetPlaying(Voice3D* voice, bool playing)
{
    if (voice == nullptr || !enabled_) return;
    voice->wantPlaying = playing;
    try {
        if (playing) {
            ApplyVoice(*voice);
            if (voice->instance->getStateProperty() != SoundState::Playing) voice->instance->Play();
        } else {
            voice->instance->Stop();
        }
    } catch (const std::exception& e) {
        School::Log("audio", "cannot change voice: %s", e.what());
    }
}

void AudioSystem::ApplyVoice(Voice3D& voice)
{
    const Vector3 d = voice.position - listener_.getPositionProperty();
    const float dist = d.Length();
    // Smooth fade over the outer third of the radius so a voice never pops in or out.
    const float edge = std::clamp((voice.radius - dist) / (voice.radius * 0.35f), 0.0f, 1.0f);
    voice.currentVolume = voice.baseVolume * edge;
    voice.emitter.setPositionProperty(voice.position);
    voice.instance->Apply3D(listener_, voice.emitter);
    voice.instance->setVolumeProperty(std::clamp(voice.currentVolume, 0.0f, 1.0f));
}

void AudioSystem::Update()
{
    if (!enabled_) return;
    try {
        for (auto& voice : loops_)
            if (voice->wantPlaying) ApplyVoice(*voice);
        for (std::size_t i = 0; i < oneShots_.size();) {
            Voice3D& v = *oneShots_[i];
            if (v.instance->getStateProperty() == SoundState::Stopped) {
                oneShots_.erase(oneShots_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ApplyVoice(v);
                ++i;
            }
        }
    } catch (const std::exception& e) {
        School::Log("audio", "update failed: %s", e.what());
    }
}

int AudioSystem::ActiveVoiceCount() const
{
    int n = 0;
    for (const auto& v : loops_)
        if (v->wantPlaying) ++n;
    return n + static_cast<int>(oneShots_.size());
}

void AudioSystem::ReleaseLoop(Voice3D* voice)
{
    if (voice == nullptr) return;
    for (auto it = loops_.begin(); it != loops_.end(); ++it) {
        if (it->get() != voice) continue;
        if (voice->instance) voice->instance->Stop();
        loops_.erase(it);
        return;
    }
}
