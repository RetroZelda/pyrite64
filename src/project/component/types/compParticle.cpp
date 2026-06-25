/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/colors.h"
#include "../../assetManager.h"
#include "../../assets/emitter.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../utils/meshGen.h"
#include "glm/geometric.hpp"

namespace Project::Component::Particle
{
  struct Data
  {
    PROP_U64(emitterUUID);
    PROP_BOOL(autoPlay);
  };

  std::shared_ptr<void> init(Object &obj) {
    auto data = std::make_shared<Data>();
    data->autoPlay.value = true;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder builder{};
    builder.set(data.emitterUUID);
    builder.set(data.autoPlay);
    return builder.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->emitterUUID);
    Utils::JSON::readProp(doc, data->autoPlay, true);
    return data;
  }

  void build(Object&, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    uint16_t id = 0xFFFF;
    auto res = ctx.assetUUIDToIdx.find(data.emitterUUID.value);
    if (res == ctx.assetUUIDToIdx.end()) {
      Utils::Logger::log("Component Particle: Emitter UUID not found: " + std::to_string(entry.uuid), Utils::Logger::LEVEL_ERROR);
    } else {
      id = res->second;
    }

    uint8_t flags = 0;
    if(data.autoPlay.value) flags |= 1 << 0;

    ctx.fileObj.write<uint16_t>(id);
    ctx.fileObj.write<uint8_t>(flags);
    ctx.fileObj.write<uint8_t>(0); // padding
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      const auto &emitters = ctx.project->getAssets().getTypeEntries(FileType::EMITTER);
      if (!ImTable::isPrefabLocked()) {
        // Write to .value directly (like compModel): serialize() only persists .value, so
        // writing through .resolve(obj) could land in the prefab-override slot and be dropped
        // on save -> build writes 0xFFFF -> the emitter never spawns in-game.
        ImTable::addAssetVecComboBox("Emitter", emitters, data.emitterUUID.value, [](auto){});
      } else {
        ImTable::add("Emitter");
        ImGui::TextDisabled("<locked>");
      }
      ImTable::addObjProp("Auto-Play", data.autoPlay);

      ImTable::end();
    }
  }

  void draw3D(Object& obj, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass)
  {
    const bool selected = ctx.isObjectSelected(obj.uuid);
    glm::vec3 pos = obj.pos.resolve(obj.propOverrides);
    Utils::Mesh::addSprite(*vp.getSprites(), pos, obj.uuid,
      2, selected ? Utils::Colors::kSelectionTint : glm::u8vec4{0xFF, 0xFF, 0xFF, 0xFF});

    if(!selected) return;

    // When selected, draw the emission bounding VOLUME and the emit DIRECTION (world axes — the
    // runtime emitter follows the object's position, not its rotation, so these aren't rotated).
    Data &data = *static_cast<Data*>(entry.data.get());
    auto *e = ctx.project->getAssets().getEntryByUUID(data.emitterUUID.value);
    if(!e || e->type != FileType::EMITTER || !e->emitter) return;
    auto &em = *e->emitter;
    auto &lines = *vp.getLines();

    const glm::vec4 shapeCol{0.40f, 0.80f, 1.00f, 1.0f}; // cyan volume
    glm::vec3 s = em.shapeSize.value;
    switch(em.emitShape.value) {
      case Assets::Emitter::SHAPE_SPHERE:
        Utils::Mesh::addLineSphere(lines, pos, glm::vec3{s.x, s.x, s.x}, shapeCol);
        break;
      case Assets::Emitter::SHAPE_BOX:
        Utils::Mesh::addLineBox(lines, pos, s, shapeCol);
        break;
      case Assets::Emitter::SHAPE_CONE:
        Utils::Mesh::addLineCone(lines, pos, s, shapeCol);
        break;
      default: { // SHAPE_POINT — a small XYZ cross marker
        const float r = 8.0f;
        Utils::Mesh::addLine(lines, pos - glm::vec3{r,0,0}, pos + glm::vec3{r,0,0}, shapeCol);
        Utils::Mesh::addLine(lines, pos - glm::vec3{0,r,0}, pos + glm::vec3{0,r,0}, shapeCol);
        Utils::Mesh::addLine(lines, pos - glm::vec3{0,0,r}, pos + glm::vec3{0,0,r}, shapeCol);
      } break;
    }

    glm::vec3 v = em.velocity.value;
    float vlen = glm::length(v);
    if(vlen > 1e-4f) {
      float len = glm::length(s) + 50.0f; // visible relative to the volume
      Utils::Mesh::addLine(lines, pos, pos + (v / vlen) * len, glm::vec4{1.0f, 0.86f, 0.35f, 1.0f});
    }
  }

  // Runs in the viewport's copy pass (before the render pass): hand this emitter to the viewport
  // so it can simulate + draw a live, in-scene billboard preview at the object's position.
  void drawCopyPass(Object& obj, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer*, SDL_GPUCopyPass*)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (data.emitterUUID.value != 0) {
      vp.submitEmitter(obj.uuid, data.emitterUUID.value, obj.pos.resolve(obj.propOverrides));
    }
  }
}
