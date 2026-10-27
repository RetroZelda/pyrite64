/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include "json.hpp"

#include "../../../renderer/camera.h"
#include "../../../renderer/vertBuffer.h"
#include "../../../renderer/framebuffer.h"
#include "../../../renderer/mesh.h"
#include "../../../renderer/object.h"
#include "../../../renderer/skeleton.h"
#include "../../../renderer/particleBatch.h"
#include "../../../renderer/particleWrap.h"
#include "../../../utils/container.h"
#include "engine/include/renderer/particles/particleSim.h"
#include "../../splineEdit.h"

namespace Renderer { class Texture; }

namespace Editor
{
  class Viewport3D
  {
    private:
      Renderer::UniformGlobal uniGlobal{};
      Renderer::Skeleton dummySkeleton;
      Renderer::Framebuffer fb{};
      Renderer::Camera camera{};
      uint32_t passId{};
      bool detached{false};

      bool isMouseHover{false};
      bool isMouseDown{false};
      Utils::RequestVal<uint32_t> pickedObjID{};
      bool pickAdditive{false};
      bool selectionPending{false};
      bool selectionDragging{false};
      bool cameraDragActive{false};
      bool cameraDragFlush{false};

      float moveSpeedModifier{1.0f};
      float vpOffsetY{};
      glm::vec2 cursorLockPos{};
      glm::vec2 mousePos{};
      glm::vec2 mousePosStart{};
      glm::vec2 mousePosClick{};
      glm::vec2 selectionStart{};
      glm::vec2 selectionEnd{};

      std::shared_ptr<Renderer::Mesh> meshGrid{};
      Renderer::Object objGrid{};

      std::shared_ptr<Renderer::Mesh> meshLines{};
      Renderer::Object objLines{};

      std::shared_ptr<Renderer::Mesh> meshSprites{};
      Renderer::Object objSprites{};

      bool showGrid{true};
      bool showCollMesh{false};
      bool showCollObj{true};
      bool showIcons{true};
      bool iconsVisible{true}; 
      bool cleanPreview{false};

      // Mirror the editor view onto a scene camera (0 = free fly).
      uint64_t boundCameraUUID{0};
      bool useCameraRes{false};
      // Free-fly camera ortho toggle. Keep separate so a bound camera's projection doesn't overwrite.
      bool freeFlyOrtho{false};
      float fbScale{1.0f}; // framebuffer pixels per displayed pixel (used for object picking)

      int gizmoOp{0};
      bool gizmoTransformActive{false};
      bool overRotGizmo{false};
      bool inputActive{false};    // this viewport captured the camera drag (started inside it)
      bool viewGizmoOwned{false}; // this viewport owns the active orientation-cube drag
      bool navLocked{false};      // viewport lock mode: cursor captured, right-click to toggle

      SplineEdit::ViewportState splineState{}; // spline point editor transients (session is global)
      bool showSplines{true};
      uint32_t curvePickUUID{0};               // CPU curve hit stored with a click, used when the GPU pick returns
      // This viewport's object gizmo was under the mouse (or in use) right after its own Manipulate(),
      // read on the next frame: it wins over the passive spline handles drawn under it. ImGuizmo keeps
      // ONE hover state, which belongs to whichever viewport called Manipulate() last.
      bool objGizmoHovered{false};

      // --- Live particle billboards for placed ParticleEmitter components ---
      // Per-OBJECT sim state (keyed by object UUID, so two objects sharing an emitter asset
      // animate independently). compParticle::draw3D feeds submissions each frame; the particle
      // section of onRenderPass advances the sims, builds world-space billboards, and draws them.
      struct EmitterSimState {
        std::vector<P64::ParticleSim::Particle> particles{};
        P64::ParticleSim::Rng rng{};
        float simTime{0.0f};
        float spawnAccum{0.0f};
        uint32_t fillEmitted{0}; // FIRE_FILL: running total emitted this cycle (caps the fill)
        bool oneShotPending{true};
        uint64_t emitterUUID{0}; // detect asset swap -> reset
        bool seen{false};        // for stale-sweep
      };
      struct EmitterSubmission { uint64_t objectUUID; uint64_t emitterUUID; glm::vec3 worldPos; glm::vec3 parentPos; };

      std::unordered_map<uint64_t, EmitterSimState> emitterStates{};
      std::vector<EmitterSubmission> emitterSubmissions{};
      std::vector<Renderer::ParticleVertex> particleVerts{};
      std::vector<Renderer::ParticleDrawGroup> particleGroups{};
      std::shared_ptr<Renderer::ParticleBatch> particleBatch{};
      Renderer::ParticleWrapPass ptxWrapPass{}; // RDP-accurate (wraparound) additive compositing

      void onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& renderScene);
      void onCopyPass(SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass *copyPass);
      void onPostRender(Renderer::Scene& renderScene);
      // simulate emitters + build/upload billboard verts (copy pass), then bind+draw (render pass)
      void buildParticles(SDL_GPUCopyPass* copyPass);
      void drawParticles(SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass, Renderer::Scene& renderScene);

      // Toggles dragging which hides the cursor for infinite movement
      void setCameraDrag(bool active);

    public:
      uint32_t winId{0};

      explicit Viewport3D(uint32_t winId = 0);
      ~Viewport3D();

      // Called by compParticle::draw3D each frame for every placed emitter (during the object loop).
      // `parentPos` = the object's parent world position (SPACE_PARENT anchor; origin if parent-less).
      void submitEmitter(uint64_t objectUUID, uint64_t emitterUUID, const glm::vec3 &worldPos,
                         const glm::vec3 &parentPos = {}) {
        emitterSubmissions.push_back({objectUUID, emitterUUID, worldPos, parentPos});
      }

      void detach();

      // Stable per-instance ImGui window title; the "###" id keeps docking state across renames.
      std::string getWindowTitle() const {
        if (winId == 0) return "3D-Viewport";
        return "3D-Viewport " + std::to_string(winId + 1) + "###vp" + std::to_string(winId);
      }

      bool isViewHovered() const { return isMouseHover; }

      /**
       * World point at the centre of this viewport's view: the free camera's orbit pivot, or a point
       * ahead of the bound scene camera. Used to place new objects where the user is looking.
       */
      glm::vec3 getViewCenter() const;

      nlohmann::json saveState() const;
      void loadState(const nlohmann::json &j);

      std::shared_ptr<Renderer::Mesh> getLines() {
        return meshLines;
      }

      std::shared_ptr<Renderer::Mesh> getSprites() {
        return meshSprites;
      }

      /**
       * Moves the focused object to the position of the 3D viewport camera and with the same rotation.
       */
      bool alignFocusedObjectToCamera();

      void draw();
  };
}
