/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "emitterPreview.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "ImGuizmo.h"
#include "IconsMaterialDesignIcons.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/quaternion.hpp"
#include "../../imgui/helper.h"
#include "../../../context.h"
#include "../../../project/assets/emitter.h"
#include "../../../renderer/texture.h"
#include "../../../renderer/scene.h"
#include "../../../renderer/n64Mesh.h"
#include "../../../utils/hash.h"
#include "../../../utils/emitterUtil.h"

namespace PS = P64::ParticleSim;

namespace
{
  // Shared with the in-scene viewport billboards so both build an identical sim config + scale.
  using Utils::Emitter::SIZE_TO_WORLD;
  using Utils::Emitter::toConfig;

  bool projectPoint(const Renderer::UniformGlobal &uni, const glm::vec4 &vp,
                    const ImVec2 &rectMin, glm::vec3 world, ImVec2 &out, float &depth)
  {
    glm::vec3 p = glm::project(world, uni.cameraMat, uni.projMat, vp);
    if(p.z < 0.0f || p.z > 1.0f) return false;
    out = {rectMin.x + p.x, rectMin.y + (vp.w - p.y)};
    depth = p.z;
    return true;
  }

  // Project a world segment and draw it if both ends are in front of the camera.
  void drawWorldLine(ImDrawList *dl, const Renderer::UniformGlobal &uni, const glm::vec4 &vp,
                     const ImVec2 &rectMin, glm::vec3 a, glm::vec3 b, ImU32 col, float thick = 1.0f)
  {
    ImVec2 sa, sb; float da, db;
    if(projectPoint(uni, vp, rectMin, a, sa, da) && projectPoint(uni, vp, rectMin, b, sb, db))
      dl->AddLine(sa, sb, col, thick);
  }
}

Editor::EmitterPreview::EmitterPreview()
  : dummySkeleton{ctx.gpu}
{
  particleBatch = std::make_shared<Renderer::ParticleBatch>(ctx.gpu);
  passId = (uint32_t)(Utils::Hash::sha256_64bit("Editor::EmitterPreview") & 0xFFFFFFFF);
  ctx.scene->addCopyPass(passId, [this](SDL_GPUCommandBuffer* c, SDL_GPUCopyPass* cp){ onCopyPass(c, cp); });
  ctx.scene->addRenderPass(passId, [this](SDL_GPUCommandBuffer* c, Renderer::Scene& s){ onRenderPass(c, s); });
}

Editor::EmitterPreview::~EmitterPreview()
{
  if(ctx.scene) {
    ctx.scene->removeCopyPass(passId);
    ctx.scene->removeRenderPass(passId);
  }
}

// Uploads the particle billboard batch built by draw(), plus the bone matrices for the
// reference model's skeleton (bind pose) before the render pass. Static models use an
// identity dummy skeleton (the storage buffer is what use() binds, so update every frame).
void Editor::EmitterPreview::onCopyPass(SDL_GPUCommandBuffer*, SDL_GPUCopyPass* copyPass)
{
  if(ptxRenderRequested && particleBatch) {
    particleBatch->setVertices(particleVerts);
    particleBatch->upload(copyPass);
  }

  if(!renderRequested || refModelUUID == 0 || !ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(refModelUUID);
  if(!asset || asset->type != Project::FileType::MODEL_3D) return;

  bool skinned = !asset->model.t3dm.skeletons.empty();
  if(skinned) {
    if(refSkelAsset != refModelUUID || !refSkel) {
      refSkel = std::make_shared<Renderer::Skeleton>(ctx.gpu, asset->model, asset->conf.baseScale);
      refSkelAsset = refModelUUID;
    }
    refSkel->update(*copyPass);
  } else {
    refSkel = nullptr;
    refSkelAsset = refModelUUID;
    dummySkeleton.update(*copyPass);
  }
}

// Runs during the scene's GPU submit; the camera/uniGlobal + fb sizes were set in draw().
// Renders the reference model (if any, cached) and then composites the particle billboards
// over it into ptxFb with the hardware-matching blend pipelines.
void Editor::EmitterPreview::onRenderPass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& scene)
{
  renderRefModel(cmdBuff, scene);

  bool wantedPtx = ptxRenderRequested;
  ptxRenderRequested = false;
  if(wantedPtx && ptxFbSize != 0) {
    renderParticlePass(cmdBuff, scene);
  }
}

// Renders the reference model into our framebuffer using the preview camera.
void Editor::EmitterPreview::renderRefModel(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& scene)
{
  // NOTE: do NOT reset hasRefRender here — when the render is skipped (cached, not dirty) the FB
  // still holds the last image and the composite must keep showing it.
  // Only render when the preview requested it this frame; consume the request.
  bool wanted = renderRequested;
  renderRequested = false;
  if(!wanted || refModelUUID == 0 || fbW == 0 || fbH == 0 || !ctx.project) return;

  auto *asset = ctx.project->getAssets().getEntryByUUID(refModelUUID);
  if(!asset || asset->type != Project::FileType::MODEL_3D || !asset->mesh3D) return;
  if(!asset->mesh3D->isLoaded()) { asset->mesh3D->recreate(*ctx.scene); return; }

  refObj.setMesh(asset->mesh3D);
  refObj.uniform = {};
  refObj.uniform.modelMat = glm::mat4(1.0f); // identity transform
  refObj.uniform.mat.flags = 0;
  refObj.setObjectID(0);

  // neutral studio lighting just for this render
  auto savedLights = scene.getLights();
  scene.clearLights();
  scene.addLight(Renderer::Light{ .color = {0.55f, 0.55f, 0.60f, 1.0f}, .type = 0 });
  scene.addLight(Renderer::Light{ .color = {1.0f, 0.98f, 0.92f, 1.0f},
    .dir = glm::normalize(glm::vec3{-0.4f, -0.7f, -0.55f}), .type = 1 });

  fb.setClearColor({24.0f / 255.0f, 26.0f / 255.0f, 32.0f / 255.0f, 1.0f});

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
    cmdBuff, fb.getTargetInfo(), fb.getTargetInfoCount(), &fb.getDepthTargetInfo());
  scene.getPipeline("n64").bind(pass);
  (refSkel ? *refSkel : dummySkeleton).use(pass);
  SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));

  std::vector<uint32_t> parts;
  parts.reserve(asset->model.t3dm.models.size());
  for(uint32_t i = 0; i < asset->model.t3dm.models.size(); ++i) parts.push_back(i);

  refObj.draw(pass, cmdBuff, Renderer::N64Mesh::ObjectRef{
    .partsIndices = parts, .model = &asset->model, .matInstance = &dummyMat, .obj = dummyObj });

  SDL_EndGPURenderPass(pass);

  scene.clearLights();
  for(auto &l : savedLights) scene.addLight(l);

  hasRefRender = true;
  refDirty = false; // FB now matches the current camera/model; skip re-render until it changes
}

// Composites the particle billboards into ptxFb: the cached reference-model render is blitted
// in as the background (or a flat clear when there is none), then the billboards draw with the
// SAME pipelines/samplers as the scene viewport — alpha/additive/opaque blending, point or
// bilinear filtering, mirrored-repeat addressing and the alpha-compare discard all match the
// RDP output. Blending happens directly against the background, so the composite is exact.
void Editor::EmitterPreview::renderParticlePass(SDL_GPUCommandBuffer* cmdBuff, Renderer::Scene& scene)
{
  const bool haveModelBg = hasRefRender && fb.getTexture();
  if(haveModelBg) {
    SDL_GPUBlitInfo blit{};
    blit.source.texture = fb.getTexture();
    blit.source.w = fbW;
    blit.source.h = fbH;
    blit.destination.texture = ptxFb.getTexture();
    blit.destination.w = ptxFbSize;
    blit.destination.h = ptxFbSize;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmdBuff, &blit);
  }

  SDL_GPUColorTargetInfo targets[2]{ptxFb.getTargetInfo()[0], ptxFb.getTargetInfo()[1]};
  if(haveModelBg) targets[0].load_op = SDL_GPU_LOADOP_LOAD; // keep the blitted background
  SDL_GPUDepthStencilTargetInfo depthInfo = ptxFb.getDepthTargetInfo();

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuff, targets, 2, &depthInfo);
  Renderer::drawParticleGroups(cmdBuff, pass, scene, *particleBatch, uniGlobal, particleGroups);
  SDL_EndGPURenderPass(pass);

  ptxHasRender = true;
}

void Editor::EmitterPreview::reset()
{
  particles.clear();
  spawnAccum = 0.0f;
  simTime = 0.0f;
  rng.seed(0x9E3779B9u); // fixed seed -> deterministic, matches engine when same-seeded
  oneShotPending = true;
  emitterPos = {0, 0, 0};
}

void Editor::EmitterPreview::draw(Project::Assets::Emitter &em, uint64_t assetUUID)
{
  auto &assets = ctx.project->getAssets();

  if(assetUUID != shownEmitterUUID) {
    shownEmitterUUID = assetUUID;
    reset();
  }

  // --- toolbar ---
  if(ImGui::Button(playing ? ICON_MDI_PAUSE : ICON_MDI_PLAY, {28_px, 0})) playing = !playing;
  ImGui::SameLine();
  if(ImGui::Button(ICON_MDI_REFRESH " Restart")) reset();
  ImGui::SameLine();
  ImGui::Checkbox("Shape", &showShape);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150_px);
  ImGui::Combo("##gizmo", &gizmoOp, "Gizmo: Off\0Gizmo: Move\0Gizmo: Rotate Dir\0");
  ImGui::SameLine();
  ImGui::Checkbox("Invert Pan", &invertPan);
  ImGui::SameLine();

  // reference-model picker (rendered behind the particles for scale reference)
  {
    const auto &models = assets.getTypeEntries(Project::FileType::MODEL_3D);
    const char* curName = "<no ref model>";
    for(const auto &m : models) if(m.getUUID() == refModelUUID) curName = m.getName().c_str();
    ImGui::SetNextItemWidth(150_px);
    if(ImGui::BeginCombo("##refModel", curName)) {
      if(ImGui::Selectable("<no ref model>", refModelUUID == 0)) refModelUUID = 0;
      for(const auto &m : models) {
        if(ImGui::Selectable(m.getName().c_str(), m.getUUID() == refModelUUID)) {
          refModelUUID = m.getUUID();
          if(m.mesh3D && !m.mesh3D->isLoaded()) m.mesh3D->recreate(*ctx.scene);
        }
      }
      ImGui::EndCombo();
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("RMB orbit | MMB pan | wheel zoom | LMB gizmo | %s recenter",
    ImGui::GetKeyName(ctx.prefs.keymap.focusObject));

  // --- view rect + input capture (all three mouse buttons) ---
  ImVec2 rectSize = ImGui::GetContentRegionAvail();
  if(rectSize.x < 16.0f || rectSize.y < 16.0f) return;
  ImVec2 rectMin = ImGui::GetCursorScreenPos();
  ImVec2 rectMax = {rectMin.x + rectSize.x, rectMin.y + rectSize.y};

  // Reserve the canvas with a NON-interactive Dummy (not an InvisibleButton): an
  // interactive item sets HoveredId, and ImGuizmo's CanActivate() refuses to grab while
  // ImGui::IsAnyItemHovered() is true — that was blocking the gizmo. Hover is computed
  // manually instead. (Same approach Viewport3D uses with its ImGui::Image.)
  ImGui::Dummy(rectSize);
  isMouseHover = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rectMin, rectMax);

  if(!cameraInit) { camera.focus({0.0f, 40.0f, 0.0f}, 220.0f); cameraInit = true; }
  camera.screenSize = {rectSize.x, rectSize.y};

  glm::vec4 vp{0.0f, 0.0f, rectSize.x, rectSize.y};

  // Camera drag: track the raw mouse and feed the CUMULATIVE delta from press
  // (orbitDelta/moveDelta capture their base on the first call). ClearActiveID() each
  // drag frame releases the canvas button so the gizmo (LMB) and non-left drags work —
  // this is the same pattern Viewport3D uses.
  auto &io = ImGui::GetIO();
  mousePos = {io.MousePos.x, io.MousePos.y};
  bool heldL = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool heldM = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  bool heldR = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  bool newDown = heldL || heldM || heldR;

  if(!isMouseDown && newDown && isMouseHover) mousePosStart = mousePos;
  isMouseDown = newDown && (isMouseDown || isMouseHover);

  glm::vec2 dragDelta = mousePos - mousePosStart;
  if(isMouseDown) {
    ImGui::ClearActiveID();
    if(heldR) {                       // RMB: orbit the view
      camera.stopMoveDelta();
      camera.orbitDelta(dragDelta);
    } else if(heldM) {                // MMB: pan (invertable)
      camera.stopRotateDelta();
      glm::vec2 pan = invertPan ? dragDelta : -dragDelta;
      camera.moveDelta(pan * 3.0f);
    }
    // LMB falls through to the gizmo (ClearActiveID above frees the canvas button for it)
  } else {
    camera.stopRotateDelta();
    camera.stopMoveDelta();
  }
  if(!newDown) isMouseDown = false;

  // Zoom proportional to the current distance: gentle up close, faster when far, never overshoots
  // the pivot (the camera clamps zoom-in to the orbit centre). NOTE: zoomSpeed is applied every
  // frame while decaying *0.9, so one wheel tick moves a TOTAL of ~zoomSpeed*10 — hence the small
  // 0.02 factor (one tick ~= 20% of the distance to the pivot).
  if(isMouseHover && io.MouseWheel != 0.0f) {
    float camDist = glm::length(camera.pos - camera.pivot);
    camera.zoomSpeed += io.MouseWheel * camDist * 0.02f;
  }
  if(isMouseHover && ImGui::IsKeyPressed(ctx.prefs.keymap.focusObject)) {
    camera.focus(emitterPos + glm::vec3(0.0f, 40.0f, 0.0f), 220.0f);
  }

  camera.update();
  camera.apply(uniGlobal);
  uniGlobal.screenSize = {rectSize.x, rectSize.y};

  // Reference-model framebuffer: allocate ONCE at a fixed resolution and NEVER resize it.
  // Resizing frees+reallocates GPU textures, which fragments the SDL Vulkan allocator and
  // triggers a defrag that crashes on a tab switch. The camera uses the rect's aspect
  // (camera.screenSize above), so rendering into a fixed square target and stretching it to
  // the view via AddImage stays undistorted.
  //
  // CACHING: re-render the model into the FB only when the camera or model changed — doing it
  // every frame was the preview's biggest cost. The AddImage composite keeps drawing the cached
  // texture regardless, so the model stays visible while the FB render is skipped.
  if(refModelUUID != lastRefRenderUUID) { refDirty = true; hasRefRender = false; lastRefRenderUUID = refModelUUID; }
  if(uniGlobal.cameraMat != lastCamMat || uniGlobal.projMat != lastProjMat) {
    refDirty = true; lastCamMat = uniGlobal.cameraMat; lastProjMat = uniGlobal.projMat;
  }
  constexpr uint32_t FB_RES = 512;
  renderRequested = (refModelUUID != 0) && refDirty; // only re-render the ref-model FB when dirty
  if(refModelUUID != 0 && fbW == 0) {
    fb.resize(FB_RES, FB_RES);
    fbW = fbH = FB_RES;
  }
  // particle composite target (allocated once, never resized — same defrag-crash caveat)
  if(ptxFbSize == 0) {
    ptxFb.resize(FB_RES, FB_RES);
    ptxFb.setClearColor({24.0f / 255.0f, 26.0f / 255.0f, 32.0f / 255.0f, 1.0f});
    ptxFbSize = FB_RES;
  }

  // --- simulation ---
  PS::Config cfg = toConfig(em);
  // density must match hardware, which runs the authored maxParticles (UI clamps to 2048)
  if(cfg.maxParticles > 2048u) cfg.maxParticles = 2048u;
  auto seq = em.textureSequence();
  cfg.swapCount = seq.empty() ? 1u : (uint32_t)seq.size();

  float dt = playing ? std::min(io.DeltaTime, 0.05f) : 0.0f;
  if(playing) {
    simTime += dt;
    uint32_t used = (uint32_t)particles.size();
    uint32_t freeSlots = cfg.maxParticles > used ? cfg.maxParticles - used : 0;
    uint32_t n = PS::spawnCount(cfg, dt, spawnAccum, freeSlots, oneShotPending);
    PS::Vec3 origin = cfg.worldSpace ? PS::Vec3{emitterPos.x, emitterPos.y, emitterPos.z} : PS::Vec3{};
    for(uint32_t i = 0; i < n; ++i) particles.push_back(PS::spawn(cfg, rng, origin));

    for(size_t i = 0; i < particles.size(); ) {
      if(!PS::step(particles[i], cfg, dt)) {
        particles[i] = particles.back();
        particles.pop_back();
      } else {
        ++i;
      }
    }
    if(cfg.fireMode == PS::FIRE_ONE_SHOT && particles.empty() && !oneShotPending) {
      oneShotPending = true; // loop the one-shot preview
    }
  }

  // --- build the particle billboards (GPU-rendered; composited into ptxFb this frame) ---
  {
    // resolve the current swap texture (none -> flat-colored quads, like the hardware path)
    Renderer::Texture* tex = nullptr;
    if(!seq.empty()) {
      uint32_t si = PS::swapIndexForTime(cfg, simTime);
      if(si >= seq.size()) si = 0;
      auto *e = assets.getEntryByUUID(seq[si]);
      if(e && e->texture && e->texture->getGPUTex()) tex = e->texture.get();
    }

    particleVerts.clear();
    particleGroups.clear();

    glm::vec3 camRight = camera.rot * glm::vec3(1, 0, 0);
    glm::vec3 camUp    = camera.rot * glm::vec3(0, 1, 0);
    glm::vec3 camFwd   = camera.rot * glm::vec3(0, 0, -1);
    Utils::Emitter::buildBillboards(particleVerts, particles,
      cfg, simTime, emitterPos, camRight, camUp, camFwd);

    if(!particleVerts.empty()) {
      auto info = Utils::Emitter::toRenderInfo(em, cfg);
      particleGroups.push_back({tex, 0, (uint32_t)particleVerts.size(),
        info.blend, info.mirrorSampler, info.bilinear, info.alphaThreshold});
    }
    ptxRenderRequested = true; // composite runs even with zero particles (background/model)
  }

  // --- render (composite image below, line/gizmo overlays on the draw list above) ---
  auto *dl = ImGui::GetWindowDrawList();
  if(ptxHasRender && ptxFb.getTexture()) {
    dl->AddImage(ImTextureRef(ptxFb.getTexture()), rectMin, rectMax); // bg/model + particles
  } else {
    dl->AddRectFilled(rectMin, rectMax, IM_COL32(24, 26, 32, 255));
  }
  dl->PushClipRect(rectMin, rectMax, true);

  // ground grid (y=0)
  {
    const ImU32 gridCol = IM_COL32(60, 64, 76, 255);
    const ImU32 axisCol = IM_COL32(90, 96, 112, 255);
    const float ext = 200.0f, stepG = 40.0f;
    for(float x = -ext; x <= ext + 0.5f; x += stepG)
      drawWorldLine(dl, uniGlobal, vp, rectMin, {x, 0, -ext}, {x, 0, ext}, x == 0.0f ? axisCol : gridCol);
    for(float z = -ext; z <= ext + 0.5f; z += stepG)
      drawWorldLine(dl, uniGlobal, vp, rectMin, {-ext, 0, z}, {ext, 0, z}, z == 0.0f ? axisCol : gridCol);
  }

  // emission shape at the emitter position
  if(showShape) {
    const ImU32 shapeCol = IM_COL32(120, 200, 255, 200);
    glm::vec3 c = emitterPos;
    glm::vec3 s = {em.shapeSize.value.x, em.shapeSize.value.y, em.shapeSize.value.z};
    switch(em.emitShape.value) {
      case Project::Assets::Emitter::SHAPE_SPHERE: {
        float r = s.x;
        const int N = 24;
        for(int axis = 0; axis < 3; ++axis) {
          glm::vec3 prev{};
          for(int i = 0; i <= N; ++i) {
            float a = (float)i / N * 6.2831853f;
            float u = std::cos(a) * r, v = std::sin(a) * r;
            glm::vec3 p = axis == 0 ? glm::vec3{0, u, v} : axis == 1 ? glm::vec3{u, 0, v} : glm::vec3{u, v, 0};
            p += c;
            if(i > 0) drawWorldLine(dl, uniGlobal, vp, rectMin, prev, p, shapeCol);
            prev = p;
          }
        }
      } break;
      case Project::Assets::Emitter::SHAPE_BOX: {
        glm::vec3 mn = c - s, mx = c + s;
        glm::vec3 v[8] = {
          {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
          {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z}};
        int e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for(auto &ed : e) drawWorldLine(dl, uniGlobal, vp, rectMin, v[ed[0]], v[ed[1]], shapeCol);
      } break;
      case Project::Assets::Emitter::SHAPE_CONE: {
        float r = s.x; const int N = 24;
        glm::vec3 prev{}; glm::vec3 apex = c + glm::vec3{0, r, 0};
        for(int i = 0; i <= N; ++i) {
          float a = (float)i / N * 6.2831853f;
          glm::vec3 p = c + glm::vec3{std::cos(a) * r, 0, std::sin(a) * r};
          if(i > 0) drawWorldLine(dl, uniGlobal, vp, rectMin, prev, p, shapeCol);
          if(i % 6 == 0) drawWorldLine(dl, uniGlobal, vp, rectMin, p, apex, shapeCol);
          prev = p;
        }
      } break;
      default: { // POINT — small cross
        float r = 6.0f;
        drawWorldLine(dl, uniGlobal, vp, rectMin, c - glm::vec3{r,0,0}, c + glm::vec3{r,0,0}, shapeCol);
        drawWorldLine(dl, uniGlobal, vp, rectMin, c - glm::vec3{0,r,0}, c + glm::vec3{0,r,0}, shapeCol);
        drawWorldLine(dl, uniGlobal, vp, rectMin, c - glm::vec3{0,0,r}, c + glm::vec3{0,0,r}, shapeCol);
      } break;
    }
  }

  // motion direction line (always shown)
  {
    glm::vec3 dir = em.velocity.value;
    float len = glm::length(dir);
    if(len > 1e-4f) {
      dir /= len;
      glm::vec3 tip = emitterPos + dir * 80.0f;
      drawWorldLine(dl, uniGlobal, vp, rectMin, emitterPos, tip, IM_COL32(255, 220, 90, 255), 2.0f);
    }
  }

  // (particle billboards were built above and render into ptxFb via the GPU pipelines —
  //  the ImGui draw list only carries the wireframe/gizmo/stat overlays now)

  dl->PopClipRect();

  // --- gizmo: move the emitter (preview-only) or rotate the motion direction ---
  if(gizmoOp != 0) {
    ImGuizmo::PushID(0x7E771FED);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(rectMin.x, rectMin.y, rectSize.x, rectSize.y);

    if(gizmoOp == 1) {
      // translate the emitter center (preview only — not saved to the asset)
      glm::mat4 model = glm::translate(glm::mat4(1.0f), emitterPos);
      if(ImGuizmo::Manipulate(glm::value_ptr(uniGlobal.cameraMat), glm::value_ptr(uniGlobal.projMat),
         ImGuizmo::TRANSLATE, ImGuizmo::WORLD, glm::value_ptr(model)))
      {
        emitterPos = glm::vec3(model[3]);
      }
    } else {
      // rotate the motion direction (saved to the asset)
      glm::vec3 dir = em.velocity.value;
      float len = glm::length(dir);
      if(len < 1e-4f) { dir = {0, 1, 0}; len = 1.0f; }
      glm::vec3 to = dir / len, from{0, 1, 0};
      float d = glm::dot(from, to);
      glm::quat q;
      if(d > 0.9999f)       q = glm::quat(1, 0, 0, 0);
      else if(d < -0.9999f) q = glm::angleAxis(3.14159265f, glm::vec3(1, 0, 0));
      else                  q = glm::angleAxis(std::acos(d), glm::normalize(glm::cross(from, to)));
      glm::mat4 model = glm::translate(glm::mat4(1.0f), emitterPos) * glm::mat4_cast(q);
      if(ImGuizmo::Manipulate(glm::value_ptr(uniGlobal.cameraMat), glm::value_ptr(uniGlobal.projMat),
         ImGuizmo::ROTATE, ImGuizmo::WORLD, glm::value_ptr(model)))
      {
        glm::vec3 newDir = glm::normalize(glm::vec3(model * glm::vec4(0, 1, 0, 0)));
        em.velocity.value = newDir * len; // preserve magnitude
      }
    }
    ImGuizmo::PopID();
  }

  // --- white-text overlay (live values) ---
  {
    char line[160];
    float y = rectMin.y + 6.0f;
    const float x = rectMin.x + 6.0f;
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    auto put = [&](const char* s){ dl->AddText({x, y}, white, s); y += 14.0f; };

    snprintf(line, sizeof(line), "%s  |  %u / %u particles  |  %s",
      cfg.fireMode == PS::FIRE_ONE_SHOT ? "One-Shot" : "Continuous",
      (uint32_t)particles.size(), cfg.maxParticles,
      cfg.worldSpace ? "World" : "Relative");
    put(line);
    snprintf(line, sizeof(line), "spawn %.0f/s   life %.2fs   reserved ~%u B",
      cfg.spawnRate, cfg.lifetime, cfg.maxParticles * 12u);
    put(line);
    snprintf(line, sizeof(line), "t=%.1fs  textures=%u%s",
      simTime, cfg.swapCount, playing ? "" : "  [PAUSED]");
    put(line);
  }
}
