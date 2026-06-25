/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assets/assetManager.h"

#include <libdragon.h>

#include "assets/assetTypes.h"
#include "lib/logger.h"
#include "scene/components/model.h"

namespace P64::NodeGraph
{
  void* load(const char* path);
}

namespace
{
  struct AssetEntry
  {
    constexpr static uint8_t FLAG_KEEP_LOADED = 1 << 0;

    const char* path{};
    void* data{};

    uint32_t getFlags() const {
      return ((uint32_t)data >> (32-8)) & 0x0F;
    }

    uint32_t getType() const {
      return (uint32_t)data >> (32-4);
    }

    void* getPointer() const {
      return (void*)(
        ((uint32_t)data & 0x00FF'FFFF)
      );
    }

    void setPointer(void* ptr) {
      uint32_t ptrMasked = (uint32_t)ptr & 0x00FF'FFFF;
      uint32_t typeMasked = (uint32_t)data & 0xFF00'0000;
      data = (void*)(ptrMasked | typeMasked);
    }
  };

  typedef void* (*LoadFunc)(const char* path);
  typedef void (*FreeFunc)(void* ref);

  struct AssetTable
  {
    uint32_t count{};
    AssetEntry entries[];
  };

  struct AssetHandler
  {
    LoadFunc fnLoad{};
    FreeFunc fnFree{};
  };

  namespace AssetType = P64::Assets::Type;

  wav64_t* wav64Load(const char* path) {
    return wav64_load(path, nullptr);
  }

  xm64player_t *xmLoad(const char* path) {
    auto* player = new xm64player_t;
    xm64player_open(player, path);
    return player;
  }

  void xmFree(xm64player_t *player) {
    xm64player_close(player);
    delete player;
  }

  void* assetLoad(const char* path) {
    return asset_load(path, nullptr);
  }

  AssetHandler assetHandler[] = {
    [AssetType::UNKNOWN]     = {(LoadFunc)assetLoad,      (FreeFunc)free          },
    [AssetType::IMAGE]       = {(LoadFunc)sprite_load,    (FreeFunc)sprite_free   },
    [AssetType::AUDIO]       = {(LoadFunc)wav64Load,      (FreeFunc)wav64_close   },
    [AssetType::FONT]        = {(LoadFunc)rdpq_font_load, (FreeFunc)rdpq_font_free},
    [AssetType::MODEL_3D]    = {(LoadFunc)t3d_model_load, (FreeFunc)t3d_model_free},
    [AssetType::CODE_OBJ]    = {nullptr,                  nullptr                 },
    [AssetType::CODE_GLOBAL] = {nullptr,                  nullptr                 },
    [AssetType::PREFAB]      = {(LoadFunc)assetLoad,      (FreeFunc)free          },
    [AssetType::NODE_GRAPH]  = {P64::NodeGraph::load,     (FreeFunc)free          },
    [AssetType::MUSIC_XM]    = {(LoadFunc)xmLoad,         (FreeFunc)xmFree        },
    // CANVAS is editor/code-gen only and never baked; keep a null entry so the array
    // stays contiguous (GCC rejects sparse/gapped designated initializers).
    [AssetType::CANVAS]      = {nullptr,                  nullptr                 },
    [AssetType::EMITTER]     = {(LoadFunc)assetLoad,      (FreeFunc)free          },
  };

  constinit AssetTable* assetTable{nullptr};
  constinit bool isInit{false};

  // Per-entry reference counts, parallel to assetTable->entries (AssetEntry::data is
  // fully packed, so there is no room for a count inside it). Only assets that are
  // acquire()'d participate; getByIndex()-only assets keep a count of 0 and are freed
  // exclusively by freeAll() (i.e. on a scene change), preserving the old behavior.
  constinit uint16_t* refCount{nullptr};

  // Frees a single loaded entry using its type's free function, unless it is
  // flagged keep-loaded. Mirrors the per-entry body of freeAll().
  void freeEntry(uint32_t idx)
  {
    auto &entry = assetTable->entries[idx];
    if(!entry.getPointer())return;
    if(entry.getFlags() & AssetEntry::FLAG_KEEP_LOADED)return;

    auto type = entry.getType();
    const auto &loader = assetHandler[type];
    if(!loader.fnFree)return;

    // Rendering is asynchronous and triple-buffered: the RSP/RDP may still be reading this
    // asset's vertex data / recorded command blocks from a prior frame's render pass. Freeing
    // it now (e.g. when an object is removed and its refcount hits zero) would pull memory out
    // from under an in-flight pass -> the RSP reads freed/realloc'd data and the T3D microcode
    // asserts. Drain the GPU first so nothing in flight still references it. This only runs when
    // an asset is genuinely freed (refcount 0 / freeAll), so it is not a per-object-removal cost.
    rspq_wait();

    void *data = (void*)((uint32_t)entry.getPointer() | 0x8000'0000);
    loader.fnFree(data);
    entry.setPointer(nullptr);
  }
}

void P64::AssetManager::init() {
  if (isInit)return;
  isInit = true;

  assetTable = (AssetTable*)asset_load("rom:/p64/a", nullptr);
  for (uint32_t i = 0; i < assetTable->count; ++i) {
    auto &entry = assetTable->entries[i];
    uint32_t offset = (uint32_t)entry.path;
    entry.path = (char*)assetTable + offset;
  }

  refCount = (uint16_t*)calloc(assetTable->count, sizeof(uint16_t));
}

void P64::AssetManager::freeAll() {
  for (uint32_t i = 0; i < assetTable->count; ++i)
  {
    freeEntry(i);
    if(refCount)refCount[i] = 0;
  }
}

void* P64::AssetManager::acquire(uint32_t idx) {
  void* res = getByIndex(idx);
  if(idx < assetTable->count && refCount && refCount[idx] < 0xFFFF) {
    refCount[idx]++;
  }
  return res;
}

void P64::AssetManager::release(uint32_t idx) {
  if(idx >= assetTable->count || !refCount)return;
  if(refCount[idx] == 0)return;
  if(--refCount[idx] == 0) {
    freeEntry(idx);
  }
}

void P64::AssetManager::freeByIndex(uint32_t idx) {
  if(idx >= assetTable->count)return;
  freeEntry(idx);
  if(refCount)refCount[idx] = 0;
}

void* P64::AssetManager::getByIndex(uint32_t idx) {
  if (idx >= assetTable->count) {
    return nullptr;
  }

  auto &entry = assetTable->entries[idx];

  void* res = entry.getPointer();
  if (!res) {
    auto type = entry.getType();
    const auto &loader = assetHandler[type];
    assertf(loader.fnLoad != nullptr, "No asset loader for type: %lu, %lu:%s", type, idx, entry.path);
    res = loader.fnLoad(entry.path);
    entry.setPointer(res);
    //debugf("Load Asset: %s | %lu\n", entry.path, type);
  } else {
    res = (void*)((uint32_t)res | 0x8000'0000);
  }

  return res;
}

const char* P64::AssetManager::getPathByIndex(uint32_t idx)
{
  if (idx >= assetTable->count) {
    return nullptr;
  }

  auto &entry = assetTable->entries[idx];
  return entry.path;
}

/*void* P64::AssetManager::getByFilePath(const std::string &path)
{
  for (uint32_t i = 0; i < assetTable->count; ++i) {
    auto &entry = assetTable->entries[i];
    if (path == entry.path) {
      return getByIndex(i);
    }
  }
  return nullptr;
}*/
