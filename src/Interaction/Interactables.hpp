#pragma once

// Things the player can use with E: doors (held open), the fire alarm, the PA microphone, the CCTV wall,
// whiteboards (what is being taught), the school bell. Each is a box in the world with a prompt and a callback.

#include <functional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"

struct Interactable {
    Microsoft::Xna::Framework::BoundingBox box;
    std::string prompt;
    std::function<void()> activate;
    int tag = -1;   // door id for doors, else -1
};

class InteractionSystem {
public:
    void Clear() { items_.clear(); focused_ = -1; }
    void Add(Interactable item) { items_.push_back(std::move(item)); }
    [[nodiscard]] const std::vector<Interactable>& Items() const { return items_; }

    /** Finds the item under the crosshair within `maxDistance` (and not behind a wall closer than `wallDistance`). */
    void Focus(const Microsoft::Xna::Framework::Ray& ray, float maxDistance, float wallDistance);
    [[nodiscard]] std::string FocusedPrompt() const { return focused_ >= 0 ? items_[static_cast<std::size_t>(focused_)].prompt : std::string(); }
    [[nodiscard]] int FocusedTag() const { return focused_ >= 0 ? items_[static_cast<std::size_t>(focused_)].tag : -1; }
    /** Runs the focused item's action; false when nothing is focused. */
    bool Activate();

private:
    std::vector<Interactable> items_;
    int focused_ = -1;
};
