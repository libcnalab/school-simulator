#include "Rendering/EffectBank.hpp"

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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

void EffectBank::WarmUp(GraphicsDevice& device)
{
    const Vector3 p(0.0f, 0.0f, 1.0f);   // three equal points: nothing is rasterised
    const VertexPositionColorTexture colourVertices[3] = {{p, Color::White, Vector2(0, 0)}, {p, Color::White, Vector2(0, 0)}, {p, Color::White, Vector2(0, 0)}};
    const VertexPositionNormalTexture litVertices[3] = {{p, Vector3(0, 1, 0), Vector2(0, 0)}, {p, Vector3(0, 1, 0), Vector2(0, 0)}, {p, Vector3(0, 1, 0), Vector2(0, 0)}};
    for (BasicEffect* e : {world_.get(), lit_.get(), flat_.get()}) {
        e->setViewProperty(Matrix::getIdentityProperty());
        e->setProjectionProperty(Matrix::getIdentityProperty());
        e->setWorldProperty(Matrix::getIdentityProperty());
    }
    for (const bool fog : {false, true}) {
        world_->setFogEnabledProperty(fog);
        world_->setTextureProperty(white_);
        Apply(*world_);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, colourVertices, 0, 1);
        flat_->setFogEnabledProperty(fog);
        Apply(*flat_);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, colourVertices, 0, 1);
        for (const bool perPixel : {false, true})
            for (const bool second : {false, true}) {
                BasicEffect& lit = BeginLit(nullptr);
                lit.setFogEnabledProperty(fog);
                lit.setPreferPerPixelLightingProperty(perPixel);
                lit.getDirectionalLight0Property().setEnabledProperty(true);
                lit.getDirectionalLight1Property().setEnabledProperty(second);
                Apply(lit);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, litVertices, 0, 1);
            }
    }
    world_->setFogEnabledProperty(false);
    flat_->setFogEnabledProperty(false);
    lit_->setFogEnabledProperty(false);
    lit_->setPreferPerPixelLightingProperty(false);
    lit_->getDirectionalLight1Property().setEnabledProperty(false);
}

void EffectBank::Apply(BasicEffect& effect)
{
    for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty()) pass.Apply();
}
