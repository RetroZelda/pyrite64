/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "emitter.h"

#include "../../utils/fs.h"
#include "../../utils/hash.h"
#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"
#include "../../build/sceneContext.h"

namespace J = Utils::JSON;

Project::Assets::Emitter::Emitter()
{
  // Sensible defaults so a freshly-created emitter shows something on screen.
  maxParticles.value = 64;
  spawnRate.value = 20.0f;
  fireMode.value = FIRE_CONTINUOUS;
  burstCount.value = 16;

  lifetime.value = 1.0f;
  lifetimeVariance.value = 0.0f;

  emitShape.value = SHAPE_POINT;
  shapeSize.value = {0.0f, 0.0f, 0.0f};

  velocity.value = {0.0f, 1.0f, 0.0f};
  speedMin.value = 20.0f;
  speedMax.value = 40.0f;
  spread.value = 20.0f;
  gravity.value = {0.0f, -30.0f, 0.0f};
  drag.value = 0.0f;

  worldSpace.value = false;
  rotationMin.value = 0.0f;
  rotationMax.value = 0.0f;
  spinMin.value = 0.0f;
  spinMax.value = 0.0f;

  colorStart.value = {1.0f, 1.0f, 1.0f, 1.0f};
  colorEnd.value = {1.0f, 1.0f, 1.0f, 0.0f};
  sizeStart.value = 1.0f;
  sizeEnd.value = 0.2f;

  texFrames.value = 1;
  frameMode.value = ANIM_OFF;
  frameRate.value = 8.0f;
  uvScrollSpeed.value = 0.0f;
  swapMode.value = ANIM_OFF;
  swapRate.value = 4.0f;
  mirrorAnim.value = false;

  // The emitter's material is always authored directly (never derived from a glTF).
  material.isCustom.value = true;
}

std::vector<uint64_t> Project::Assets::Emitter::textureSequence() const
{
  std::vector<uint64_t> seq{};
  if (material.tex0.texUUID.value != 0) {
    seq.push_back(material.tex0.texUUID.value);
  }
  for (auto uuid : swapTextures) {
    if (uuid != 0) seq.push_back(uuid);
  }
  return seq;
}

nlohmann::json Project::Assets::Emitter::serialize() const
{
  auto doc = Utils::JSON::Builder{}
    .set(maxParticles)
    .set(spawnRate)
    .set(fireMode)
    .set(burstCount)
    .set(lifetime)
    .set(lifetimeVariance)
    .set(emitShape)
    .set(shapeSize)
    .set(velocity)
    .set(speedMin)
    .set(speedMax)
    .set(spread)
    .set(gravity)
    .set(drag)
    .set(worldSpace)
    .set(rotationMin)
    .set(rotationMax)
    .set(spinMin)
    .set(spinMax)
    .set(colorStart)
    .set(colorEnd)
    .set(sizeStart)
    .set(sizeEnd)
    .set(texFrames)
    .set(frameMode)
    .set(frameRate)
    .set(uvScrollSpeed)
    .set(swapMode)
    .set(swapRate)
    .set(mirrorAnim)
    .doc;

  doc["uuid"] = uuid;
  doc["name"] = name;
  doc["material"] = material.serialize();
  doc["swapTextures"] = swapTextures;
  return doc;
}

void Project::Assets::Emitter::deserialize(const nlohmann::json &doc)
{
  J::readProp(doc, maxParticles);
  J::readProp(doc, spawnRate);
  J::readProp(doc, fireMode);
  J::readProp(doc, burstCount);
  J::readProp(doc, lifetime);
  J::readProp(doc, lifetimeVariance);
  J::readProp(doc, emitShape);
  J::readProp(doc, shapeSize);
  J::readProp(doc, velocity);
  J::readProp(doc, speedMin);
  J::readProp(doc, speedMax);
  J::readProp(doc, spread);
  J::readProp(doc, gravity);
  J::readProp(doc, drag);
  J::readProp(doc, worldSpace);
  J::readProp(doc, rotationMin);
  J::readProp(doc, rotationMax);
  J::readProp(doc, spinMin);
  J::readProp(doc, spinMax);
  J::readProp(doc, colorStart);
  J::readProp(doc, colorEnd);
  J::readProp(doc, sizeStart);
  J::readProp(doc, sizeEnd);
  J::readProp(doc, texFrames);
  J::readProp(doc, frameMode);
  J::readProp(doc, frameRate);
  J::readProp(doc, uvScrollSpeed);
  J::readProp(doc, swapMode);
  J::readProp(doc, swapRate);
  J::readProp(doc, mirrorAnim);

  if (doc.contains("material")) {
    material.deserialize(doc["material"]);
  }

  swapTextures.clear();
  if (doc.contains("swapTextures") && doc["swapTextures"].is_array()) {
    for (const auto &v : doc["swapTextures"]) {
      swapTextures.push_back(v.get<uint64_t>());
    }
  }
}

void Project::Assets::Emitter::build(Utils::BinaryFile &file, Build::SceneCtx &sceneCtx) const
{
  // ---- .emit64 layout (big-endian, matches engine P64::PTX::EmitterDef, packed) ----
  // Material "set" flags (which optional render settings to apply at runtime).
  constexpr uint32_t MAT_BLENDER  = 1 << 0;
  constexpr uint32_t MAT_FOG      = 1 << 1;
  constexpr uint32_t MAT_ALPHACMP = 1 << 2;
  constexpr uint32_t MAT_FILTER   = 1 << 3;
  constexpr uint32_t MAT_PRIM     = 1 << 4;
  constexpr uint32_t MAT_ENV      = 1 << 5;

  auto seq = textureSequence();
  uint8_t texCount = (uint8_t)std::min<size_t>(seq.size(), 255);

  // header
  file.write<uint16_t>(0xE117);                 // magic
  file.write<uint16_t>(1);                      // version
  file.write<uint16_t>((uint16_t)std::min<uint32_t>(maxParticles.value, 0xFFFF));
  file.write<uint8_t>((uint8_t)fireMode.value);
  file.write<uint8_t>((uint8_t)emitShape.value);
  file.write<float>(spawnRate.value);
  file.write<uint16_t>((uint16_t)std::min<uint32_t>(burstCount.value, 0xFFFF));
  file.write<uint8_t>(worldSpace.value ? 1 : 0);
  file.write<uint8_t>(texCount);

  // lifetime
  file.write<float>(lifetime.value);
  file.write<float>(lifetimeVariance.value);

  // shape + motion
  file.write<float>(shapeSize.value.x); file.write<float>(shapeSize.value.y); file.write<float>(shapeSize.value.z);
  file.write<float>(velocity.value.x);  file.write<float>(velocity.value.y);  file.write<float>(velocity.value.z);
  file.write<float>(speedMin.value);    file.write<float>(speedMax.value);    file.write<float>(spread.value);
  file.write<float>(gravity.value.x);   file.write<float>(gravity.value.y);   file.write<float>(gravity.value.z);
  file.write<float>(drag.value);

  // rotation
  file.write<float>(rotationMin.value); file.write<float>(rotationMax.value);
  file.write<float>(spinMin.value);     file.write<float>(spinMax.value);

  // color + size over life
  file.writeRGBA(colorStart.value);
  file.writeRGBA(colorEnd.value);
  file.write<float>(sizeStart.value);
  file.write<float>(sizeEnd.value);

  // texture animation
  file.write<uint8_t>((uint8_t)std::min<uint32_t>(texFrames.value, 255));
  file.write<uint8_t>((uint8_t)frameMode.value);
  file.write<uint8_t>((uint8_t)swapMode.value);
  file.write<uint8_t>(mirrorAnim.value ? 1 : 0);
  file.write<float>(frameRate.value);
  file.write<float>(uvScrollSpeed.value);
  file.write<float>(swapRate.value);

  // material / blend (compact subset used to build the rspq setup block)
  uint32_t matFlags = 0;
  if(material.blenderSet.value)   matFlags |= MAT_BLENDER;
  if(material.fogSet.value)       matFlags |= MAT_FOG;
  if(material.alphaCompSet.value) matFlags |= MAT_ALPHACMP;
  if(material.filterSet.value)    matFlags |= MAT_FILTER;
  if(material.primColorSet.value) matFlags |= MAT_PRIM;
  if(material.envColorSet.value)  matFlags |= MAT_ENV;
  file.write<uint32_t>(matFlags);
  file.write<uint64_t>(material.cc.value);
  file.write<uint32_t>(material.blender.value);
  file.write<uint32_t>(material.fog.value);
  file.write<int32_t>(material.alphaComp.value);
  file.write<int32_t>(material.filter.value);
  file.write<uint32_t>(material.drawFlags.value);
  file.writeRGBA(material.primColor.value);
  file.writeRGBA(material.envColor.value);

  // texture index array (resolved to ROM asset indices; 0xFFFF if missing)
  for(uint8_t i = 0; i < texCount; ++i) {
    auto it = sceneCtx.assetUUIDToIdx.find(seq[i]);
    file.write<uint16_t>(it == sceneCtx.assetUUIDToIdx.end() ? 0xFFFF : (uint16_t)it->second);
  }
}

void Project::Assets::Emitter::load(const std::string &filePath)
{
  path = filePath;
  auto text = Utils::FS::loadTextFile(filePath);
  if (text.empty()) return;

  auto doc = nlohmann::json::parse(text, nullptr, false);
  if (!doc.is_object()) return;

  uuid = doc.value("uuid", uint64_t{0});
  if (uuid == 0) uuid = Utils::Hash::randomU64();
  name = doc.value("name", name);

  deserialize(doc);
}

void Project::Assets::Emitter::save(const std::string &filePath) const
{
  Utils::FS::saveTextFile(filePath, serialize().dump(2));
}
