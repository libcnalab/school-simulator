#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

/**
 * The stock BasicEffect instances the school shares. Exhibits configure them
 * (textures, colours, lights) immediately before drawing; nothing relies on a
 * previous configuration surviving.
 */
class EffectBank {
public:
    EffectBank(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
               Microsoft::Xna::Framework::Graphics::Texture2D* whiteTexture);

    /** Static architecture: vertex colour (baked light) times texture, no runtime lighting. */
    Microsoft::Xna::Framework::Graphics::BasicEffect& World() { return *world_; }
    /** Dynamic objects: runtime BasicEffect lighting; the caller picks texture and colours. */
    Microsoft::Xna::Framework::Graphics::BasicEffect& Lit() { return *lit_; }
    /** Vertex colours only, no texture or lighting: lines, gizmos, flat colour quads. */
    Microsoft::Xna::Framework::Graphics::BasicEffect& Flat() { return *flat_; }

    /**
     * Prepares Lit() for an object lit by BasicEffect. `texture` may be null for a plain colour.
     * A texture is bound even then: on the EasyGL renderers the lit BasicEffect program samples
     * its texture unit unconditionally, so a lit effect with TextureEnabled=false draws black
     * (see HANDOFF.md, "CNA findings"). A 1x1 white texture makes that case correct everywhere.
     */
    Microsoft::Xna::Framework::Graphics::BasicEffect& BeginLit(Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr);

    void SetCamera(const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection);

    /** Applies the effect's current technique pass. */
    static void Apply(Microsoft::Xna::Framework::Graphics::BasicEffect& effect);

private:
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> world_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> lit_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> flat_;
    Microsoft::Xna::Framework::Graphics::Texture2D* white_;
};
