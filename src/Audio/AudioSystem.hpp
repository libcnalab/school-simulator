#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

/**
 * A positional sound: a looping SoundEffectInstance plus its emitter. `radius` is
 * the distance beyond which the voice is silent; inside it CNA's Apply3D supplies
 * pan and distance attenuation and the voice adds a smooth fade towards the edge.
 */
struct Voice3D {
    std::unique_ptr<Microsoft::Xna::Framework::Audio::SoundEffectInstance> instance;
    Microsoft::Xna::Framework::Audio::AudioEmitter emitter;
    Microsoft::Xna::Framework::Vector3 position;
    float baseVolume = 1.0f;
    float radius = 12.0f;
    float currentVolume = 0.0f;
    bool wantPlaying = false;
};

/**
 * Thin owner of the school's SoundEffects, listener and voices. Every call is a
 * silent no-op when audio is disabled or no audio device could be opened, so the
 * rest of the school never has to ask.
 */
class AudioSystem {
public:
    AudioSystem(Microsoft::Xna::Framework::Content::ContentManager& content, bool enabled, float masterVolume);

    [[nodiscard]] bool Enabled() const { return enabled_; }
    [[nodiscard]] float MasterVolume() const { return master_; }
    void SetMasterVolume(float volume);
    [[nodiscard]] bool Muted() const { return muted_; }
    void SetMuted(bool muted);

    /** Moves the listener (the camera). Call once per frame before UpdateVoices(). */
    void SetListener(const Microsoft::Xna::Framework::Vector3& position,
                     const Microsoft::Xna::Framework::Vector3& forward);

    /** Plays a short sound at constant volume in the head (UI clicks, footsteps). */
    void PlayUi(const std::string& name, float volume = 1.0f, float pitch = 0.0f);
    /** Plays a short sound from a point in the world. */
    void PlayAt(const std::string& name, const Microsoft::Xna::Framework::Vector3& position, float volume = 1.0f,
                float radius = 12.0f);

    /** Creates (but does not start) a looping positional voice. Returned pointer lives as long as the system. */
    Voice3D* CreateLoop(const std::string& name, const Microsoft::Xna::Framework::Vector3& position,
                        float baseVolume, float radius);
    /** Stops and destroys a loop created by CreateLoop(). */
    void ReleaseLoop(Voice3D* voice);
    /** Starts/stops a loop. Stopped voices keep their instance so they can resume. */
    void SetPlaying(Voice3D* voice, bool playing);

    /** Updates every voice from the listener position, retires finished one-shots. */
    void Update();

    [[nodiscard]] int ActiveVoiceCount() const;

private:
    Microsoft::Xna::Framework::Audio::SoundEffect* Effect(const std::string& name);
    void ApplyVoice(Voice3D& voice);

    Microsoft::Xna::Framework::Content::ContentManager& content_;
    bool enabled_;
    bool muted_ = false;
    float master_;
    std::map<std::string, std::unique_ptr<Microsoft::Xna::Framework::Audio::SoundEffect>> effects_;
    Microsoft::Xna::Framework::Audio::AudioListener listener_;
    std::vector<std::unique_ptr<Voice3D>> loops_;
    std::vector<std::unique_ptr<Voice3D>> oneShots_;
};
