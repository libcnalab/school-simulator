#pragma once

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

// SpriteBatch.End() leaves the device in whatever state it needed, and every
// exhibit changes states on purpose. These helpers set the complete 3D state
// explicitly so nothing depends on what ran before.
namespace RenderStates {

/** Opaque 3D: depth test + write, counter-clockwise front faces (CullClockwise), anisotropic wrap sampling. */
inline void SetOpaque3D(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setRasterizerStateProperty(RasterizerState::CullClockwise);
    device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
}

/** Transparent 3D: depth test but no depth write, so overlapping panes blend instead of occluding. */
inline void SetTransparent3D(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                             const Microsoft::Xna::Framework::Graphics::BlendState& blend)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    device.setBlendStateProperty(blend);
    device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
}

/**
 * Unbinds the first texture slots. XNA (and CNA) refuse Texture2D.SetData on a texture that is still set on the
 * device ("The texture resource is in use"), so call this before rewriting a texture that was drawn last frame.
 */
inline void UnbindTextures(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
{
    for (int i = 0; i < 4; ++i) device.getTexturesProperty()(i, nullptr);
}

}  // namespace RenderStates
