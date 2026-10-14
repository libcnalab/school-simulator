#include "Interaction/Interactables.hpp"

#include <optional>

using namespace Microsoft::Xna::Framework;

void InteractionSystem::Focus(const Ray& ray, float maxDistance, float wallDistance)
{
    focused_ = -1;
    float best = maxDistance;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const std::optional<float> d = items_[i].box.Intersects(ray);
        if (!d || *d > best || *d > wallDistance + 0.3f) continue;
        best = *d;
        focused_ = static_cast<int>(i);
    }
}

bool InteractionSystem::Activate()
{
    if (focused_ < 0) return false;
    const Interactable& item = items_[static_cast<std::size_t>(focused_)];
    if (item.activate) item.activate();
    return true;
}
