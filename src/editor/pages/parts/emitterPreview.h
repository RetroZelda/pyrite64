/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "../../../renderer/camera.h"
#include "../../../renderer/uniforms.h"
#include "../../../renderer/framebuffer.h"
#include "../../../renderer/object.h"
#include "../../../renderer/skeleton.h"
#include "../../../project/scene/object.h"
#include "../../../project/component/shared/materialInstance.h"
#include "engine/include/renderer/particles/particleSim.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

namespace Project::Assets { struct Emitter; }
namespace Renderer { class Scene; }

namespace Editor
{
  // Live 3D preview of a particle emitter, rendered with ImGui's draw list (camera-facing
  // textured billboards). It runs the SHARED P64::ParticleSim so it matches the engine,
  // and resolves the emitter's texture(s) to GPU textures for display. An optional
  // reference model is rendered (n64 GPU pipeline -> framebuffer) behind the particles.
  class EmitterPreview
  {
    private:
      Renderer::Camera camera{};
      Renderer::UniformGlobal uniGlobal{};

      // Reference-model rendering (composited behind the particle billboards)
      Renderer::Framebuffer fb{};
      Renderer::Object refObj{};
      Renderer::Skeleton dummySkeleton;
      std::shared_ptr<Renderer::Skeleton> refSkel{};  // bind-pose skeleton for skinned ref models
      uint64_t refSkelAsset{0};
      Project::Object dummyObj{};
      Project::Component::Shared::MaterialInstance dummyMat{};
      uint32_t passId{0};
      uint64_t refModelUUID{0};
      uint32_t fbW{0}, fbH{0};
      bool hasRefRender{false};
      // set by draw() each frame the preview is visible; the copy/render passes (which run
      // every frame for the editor's lifetime) only touch the framebuffer when this is set,
      // so a hidden preview never renders its target (avoids a GPU defrag crash on tab switch).
      bool renderRequested{false};

      // Reference-model FB caching: re-render the model into the FB only when the camera or
      // model changed (the single biggest per-frame cost when the Particles tab is open).
      glm::mat4 lastCamMat{0.0f};
      glm::mat4 lastProjMat{0.0f};
      uint64_t lastRefRenderUUID{0xFFFFFFFFFFFFFFFFull};
      bool refDirty{true};

      void onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass* copyPass);
      void onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& scene);

      std::vector<P64::ParticleSim::Particle> particles{};
      P64::ParticleSim::Rng rng{};
      float spawnAccum{0.0f};
      float simTime{0.0f};
      bool playing{true};
      bool oneShotPending{true};

      // emitter world position in the preview (preview-only; moved via the translate gizmo)
      glm::vec3 emitterPos{0.0f};

      bool showShape{true};   // draw the emission shape
      bool invertPan{false};  // reverse the middle-mouse pan direction
      int gizmoOp{0};         // 0 = off, 1 = move emitter, 2 = rotate motion direction

      // manual mouse tracking for camera drag (mirrors Viewport3D; InvisibleButton + IsItemActive
      // doesn't cooperate with ImGuizmo / non-left buttons, so we track raw mouse + ClearActiveID)
      glm::vec2 mousePos{};
      glm::vec2 mousePosStart{};
      bool isMouseDown{false};
      bool isMouseHover{false};

      uint64_t shownEmitterUUID{0};
      bool cameraInit{false};

      void reset();

    public:
      EmitterPreview();
      ~EmitterPreview();

      // Draws the preview for `emitter` (identified by `assetUUID`) into the current window.
      void draw(Project::Assets::Emitter &emitter, uint64_t assetUUID);
  };
}
