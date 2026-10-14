#include "Rendering/EffectBank.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

EffectBank::EffectBank(GraphicsDevice& device, Texture2D* whiteTexture) : white_(whiteTexture)
{
    world_ = std::make_unique<BasicEffect>(device);
    world_->setLightingEnabledProperty(false);
    world_->setTextureEnabledProperty(true);
    world_->setVertexColorEnabledProperty(true);

    lit_ = std::make_unique<BasicEffect>(device);
    lit_->setLightingEnabledProperty(true);
    lit_->setVertexColorEnabledProperty(false);
    lit_->setTextureEnabledProperty(false);

    flat_ = std::make_unique<BasicEffect>(device);
    flat_->setLightingEnabledProperty(false);
    flat_->setTextureEnabledProperty(false);
    flat_->setVertexColorEnabledProperty(true);
}

BasicEffect& EffectBank::BeginLit(Texture2D* texture)
{
    lit_->setLightingEnabledProperty(true);
    lit_->setVertexColorEnabledProperty(false);
    lit_->setTextureEnabledProperty(true);
    lit_->setTextureProperty(texture != nullptr ? texture : white_);
    return *lit_;
}

void EffectBank::SetCamera(const Matrix& view, const Matrix& projection)
{
    for (BasicEffect* e : {world_.get(), lit_.get(), flat_.get()}) {
        e->setViewProperty(view);
        e->setProjectionProperty(projection);
    }
}

void EffectBank::Apply(BasicEffect& effect)
{
    for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty()) pass.Apply();
}
