#include "Audio/SchoolAudio.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using School::Room;
using School::RoomId;
using School::RoomType;

const char* SchoolAudio::AmbienceFor(const Room& room, const School::SchoolSim& sim, float& baseVolume, float& radius) const
{
    const int people = sim.OccupantsOf(room.id);
    radius = std::max(10.0f, std::max(room.rect.Width(), room.rect.Depth()) * 0.9f + 6.0f);
    baseVolume = 0.0f;
    switch (room.type) {
    case RoomType::Classroom:
    case RoomType::LanguageRoom:
    case RoomType::PhysicsLab:
    case RoomType::ChemistryLab:
    case RoomType::BiologyLab:
    case RoomType::ArtStudio:
        if (people < 3) return nullptr;
        baseVolume = std::min(0.45f, 0.1f + static_cast<float>(people) * 0.012f);
        return room.type == RoomType::Classroom || room.type == RoomType::LanguageRoom || room.type == RoomType::ArtStudio ? "classroom" : "lab";
    case RoomType::ComputerLab:
    case RoomType::ServerRoom:
        baseVolume = room.type == RoomType::ServerRoom ? 0.5f : 0.22f + std::min(0.2f, static_cast<float>(people) * 0.01f);
        return "computer";
    case RoomType::Cafeteria:
        if (people < 4) return nullptr;
        baseVolume = std::min(0.8f, 0.15f + static_cast<float>(people) * 0.006f);
        radius = 45.0f;
        return "cafeteria";
    case RoomType::Gym:
        if (people < 2) return nullptr;
        baseVolume = std::min(0.7f, 0.2f + static_cast<float>(people) * 0.01f);
        radius = 45.0f;
        return "gym";
    case RoomType::MusicRoom:
        if (people < 3) return nullptr;
        baseVolume = 0.5f;
        radius = 30.0f;
        return "music";
    case RoomType::Workshop:
    case RoomType::Boiler:
        baseVolume = room.type == RoomType::Boiler ? 0.6f : (people > 2 ? 0.5f : 0.0f);
        return baseVolume > 0.0f ? "machine" : nullptr;
    case RoomType::Library:
        if (people < 2) return nullptr;
        baseVolume = 0.12f;
        radius = 30.0f;
        return "hall";
    case RoomType::Lobby:
        if (people < 4) return nullptr;
        baseVolume = std::min(0.5f, 0.1f + static_cast<float>(people) * 0.01f);
        radius = 30.0f;
        return "hall";
    case RoomType::Kitchen:
        if (people < 1) return nullptr;
        baseVolume = 0.35f;
        return "machine";
    default: return nullptr;
    }
}

void SchoolAudio::Update(float dt, const Vector3& listener, const Vector3& forward, School::SchoolSim& sim, const WorldRenderer& world, RoomId playerRoom, bool outdoors)
{
    audio_.SetListener(listener, forward);
    listener_ = listener;
    outdoors_ = outdoors;
    // Doors near the player.
    int doorSounds = 0;
    for (const School::SchoolSim::DoorSound& d : sim.TakeDoorSounds()) {
        const School::Door& def = plan_.door(d.door);
        const Vector3 at(def.pos.x, plan_.FloorY(def.level) + 1.2f, def.pos.z);
        if (Vector3::DistanceSquared(at, listener) > 30.0f * 30.0f || doorSounds > 6) continue;
        ++doorSounds;
        const char* name = def.kind == School::DoorKind::Automatic || def.kind == School::DoorKind::ElevatorDoor ? "door_slide" : (d.opening ? "door_open" : "door_close");
        audio_.PlayAt(name, at, def.kind == School::DoorKind::External ? 0.7f : 0.45f, 22.0f);
    }
    // Room ambiences: rescan a few times a second.
    scanTimer_ += dt;
    if (scanTimer_ >= 0.4f) {
        scanTimer_ = 0.0f;
        struct Want {
            float score;
            RoomId id;
            const char* name;
            float volume, radius;
        };
        std::vector<Want> wants;
        for (const RoomId id : world.LoadedRooms()) {
            const Room& r = plan_.room(id);
            if (!r.Indoor()) continue;
            float vol, rad;
            const char* name = AmbienceFor(r, sim, vol, rad);
            if (name == nullptr || vol <= 0.0f) continue;
            const Vector3 c(r.rect.Center().x, plan_.FloorY(r.level) + 1.5f, r.rect.Center().z);
            const float d = Vector3::Distance(c, listener);
            if (d > rad) continue;
            wants.push_back({d, id, name, vol, rad});
        }
        std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.score < b.score; });
        if (wants.size() > 8) wants.resize(8);
        std::map<int, Loop> next;
        for (const Want& w : wants) {
            Loop l;
            auto it = loops_.find(w.id);
            if (it != loops_.end()) {
                l = it->second;
                loops_.erase(it);
            } else {
                const Room& r = plan_.room(w.id);
                l.voice = audio_.CreateLoop(w.name, Vector3(r.rect.Center().x, plan_.FloorY(r.level) + 1.5f, r.rect.Center().z), 0.0f, w.radius);
                if (l.voice != nullptr) audio_.SetPlaying(l.voice, true);
            }
            l.target = w.volume;
            if (l.voice != nullptr) next[w.id] = l;
        }
        for (auto& [id, l] : loops_) audio_.ReleaseLoop(l.voice);
        loops_ = std::move(next);
        // Chatter: people walking or loitering within earshot of the player (not those seated in a lesson).
        int near = 0;
        const School::Population& pop = sim.Pop();
        for (const School::Npc& n : pop.npcs) {
            const School::Agent& a = sim.AgentOf(n.id);
            if (a.mode == School::Mode::Offsite) continue;
            if (a.mode == School::Mode::AtSpot && (a.activity == School::Activity::InLesson || a.activity == School::Activity::Teaching || a.activity == School::Activity::Working)) continue;
            School::Vec3 p;
            float yaw;
            RoomId room;
            sim.Observe(n.id, p, yaw, room);
            const float dx = p.x - listener.X, dz = p.z - listener.Z, dy = p.y - listener.Y;
            if (dx * dx + dz * dz < 18.0f * 18.0f && std::fabs(dy) < 3.0f) ++near;
        }
        chatterLevel_ = std::min(0.7f, static_cast<float>(near) * 0.035f);
        if (chatter_ == nullptr) {
            chatter_ = audio_.CreateLoop("chatter", listener, 0.0f, 1e6f);
            if (chatter_) audio_.SetPlaying(chatter_, true);
        }
        if (outdoor_ == nullptr) {
            outdoor_ = audio_.CreateLoop("outdoor", listener, 0.0f, 1e6f);
            if (outdoor_) audio_.SetPlaying(outdoor_, true);
        }
    }
    const float k = std::min(1.0f, dt * 2.5f);
    for (auto& [id, l] : loops_) {
        l.voice->baseVolume += (l.target - l.voice->baseVolume) * k;
    }
    if (chatter_ != nullptr) {
        chatter_->position = listener;
        chatter_->baseVolume += (chatterLevel_ - chatter_->baseVolume) * k;
    }
    if (outdoor_ != nullptr) {
        outdoor_->position = listener;
        const float target = outdoors ? 0.3f : 0.06f;
        outdoor_->baseVolume += (target - outdoor_->baseVolume) * k;
    }
    if (weather_ != nullptr) {
        weather_->position = listener;
        const float target = weatherName_ == "rain" ? (outdoors ? 0.55f : 0.12f) : 0.0f;
        weather_->baseVolume += (target - weather_->baseVolume) * k;
    }
    (void)playerRoom;
}

void SchoolAudio::Footstep(bool running, bool hardFloor)
{
    ++stepIndex_;
    const float pitch = static_cast<float>((stepIndex_ % 5) - 2) * 0.05f;
    audio_.PlayUi(hardFloor ? "step_hard" : "step", running ? 0.5f : 0.32f, pitch);
}

void SchoolAudio::Announce(int which)
{
    // The PA speakers hang in every corridor and hall: the nearest few play, so the message comes from the building,
    // not from inside the listener's head. Outdoors (no speaker near) a quiet copy plays from the nearest one anyway.
    const char* name = which % 2 == 0 ? "announce1" : "announce2";
    struct Speaker {
        float d2;
        Vector3 at;
    };
    std::vector<Speaker> near;
    for (const School::Room& r : plan_.rooms) {
        if (!r.Indoor() || !School::Info(r.type).circulation) continue;
        const Vector3 at(r.rect.Center().x, plan_.FloorY(r.level) + 2.6f, r.rect.Center().z);
        const float d2 = Vector3::DistanceSquared(at, listener_);
        if (d2 < 45.0f * 45.0f) near.push_back({d2, at});
    }
    std::sort(near.begin(), near.end(), [](const Speaker& a, const Speaker& b) { return a.d2 < b.d2; });
    if (near.size() > 4) near.resize(4);
    if (near.empty()) {
        audio_.PlayUi("chime", 0.25f);
        audio_.PlayUi(name, 0.3f);
        return;
    }
    for (const Speaker& sp : near) {
        audio_.PlayAt("chime", sp.at, 0.5f, 40.0f);
        audio_.PlayAt(name, sp.at, 0.6f, 40.0f);
    }
}

void SchoolAudio::Bell(bool alarm)
{
    audio_.PlayUi(alarm ? "alarm" : "bell", alarm ? 0.8f : 0.7f);
}

void SchoolAudio::SetWeather(const std::string& weather)
{
    weatherName_ = weather;
    if (weather == "rain" && weather_ == nullptr) {
        weather_ = audio_.CreateLoop("rain", Vector3::Zero, 0.0f, 1e6f);
        if (weather_) audio_.SetPlaying(weather_, true);
    }
}

void SchoolAudio::Click()
{
    audio_.PlayUi("click", 0.5f);
}
