# CNA coverage

What the school uses, and where. The simulation half uses nothing from CNA by design.

| CNA area | Used by |
|---|---|
| `Game`, `GraphicsDeviceManager`, `GameTime`, `GameWindow` (title, resizing), `IsActive`, `Exit` | `App/SchoolGame.cpp` |
| `GraphicsProfile::HiDef`, `PresentationParameters`, `GetBackBufferData` (screenshots) | `SchoolGame::SaveScreenshot` (through cna-engine's `CaptureBackBuffer`) |
| `BasicEffect` (vertex colour x texture, lit with directional lights, fog) | `Rendering/EffectBank.cpp`, world (through cna-engine's stock binder), doors, people, sky |
| `VertexBuffer`, `IndexBuffer` (16-bit), `VertexPositionColorTexture`, `VertexPositionNormalTexture` | `StaticMesh`, `LitMesh` |
| `DynamicVertexBuffer` with `SetDataOptions::Discard` | `NpcRenderer`, `SkyDome`, `ParticleRenderer` |
| `DrawIndexedPrimitives`, `DrawUserPrimitives` | meshes; sky billboards, screen quads, debug lines |
| `BlendState`, `DepthStencilState`, `RasterizerState::CullClockwise`, `SamplerState` | `RenderStates.hpp` |
| `Texture2D` with CPU mip chains (`SetData` per level) | `TextureLibrary` (through cna-engine's `Graphics/Image.hpp`) |
| `RenderTarget2D` (colour + Depth24 for cameras, colour only for signage, boards, plates, diagnostics), `SetRenderTarget` | `Rendering/Screens.cpp` |
| `BasicEffect` lit with alpha (translucent glassware), `PreferPerPixelLighting` with specular (the art still life), two directional lights, `Matrix::CreateRotationX/Y/Z` hierarchies | `Rendering/RoomEffects.cpp`, `NpcRenderer.cpp` |
| `BlendState::Additive` billboards and ground quads with `DrawUserPrimitives` (lamp glow at night, sun and stars) | `WorldRenderer::DrawLamps`, `SkyDome` |
| `SpriteBatch`, `SpriteFont` (built from a generated atlas through the public constructor), `MeasureString` | `UI/Hud.cpp`, `UI/Minimap.cpp`, `UI/FontLoader.cpp`, screens |
| `Matrix` (look-at, perspective, rotations, scale, translation), `Vector2/3`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`, `MathHelper` | everywhere in the app |
| `Keyboard`, `Mouse` (relative mode EXT, SetPosition), `GamePad` (sticks, buttons, keyboard emulation EXT) | `Interaction/InputState.hpp`, `PlayerController.cpp` |
| `SoundEffect`, `SoundEffectInstance`, `AudioListener`, `AudioEmitter`, `Apply3D`, master volume, distance scale | `Audio/AudioSystem.cpp`, `SchoolAudio.cpp` |
| `ContentManager` (`Load<Texture2D>`, `Load<SoundEffect>`), `TitleLocation` | `TextureLibrary`, `AudioSystem`, `AssetPaths` |
| `GraphicsCapability::ThreeD`, `GetGraphicsRendererName` | start-up checks and the HUD |

Not used yet: `Model`/glTF loading, `SkinnedEffect`, `EnvironmentMapEffect`, `TextureCube`, `OcclusionQuery`,
`Video`, networking, gamer services, storage, the content pipeline.
