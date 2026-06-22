/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include <string>

namespace P64::AssetManager
{
  void init();

  /**
   * Frees all currently loaded assets.
   * This should never be called manually, and is done automatically during a scene change.
   */
  void freeAll();

  /**
   * Returns an asset handle by its asset-index.
   * Internally a table is used to prevent loading the same asset multiple time.
   * So this function will cause a load if not already loaded, and return the same point on subsequent calls.
   * During a scene-transition, all assets will be freed, so don't keep any pointers around between scenes.
   *
   * The index can be determined at build time with the provided '_asset' suffix for string.
   * For example:
   * logoPyrite = (sprite_t*)AssetManager::getByIndex("ui/logoPyrite.sprite"_asset);
   *
   * @param idx index, the `_asset` suffix can be used
   * @return pointer to asset, nullptr if not found
   */
  void* getByIndex(uint32_t idx);

  /**
   * Like getByIndex(), but also increments the asset's reference count.
   * Use this from components that own a per-object asset so the asset can be
   * reclaimed once nothing references it anymore. Every acquire() must be paired
   * with exactly one release(). Transient/one-off lookups can keep using
   * getByIndex() (those do not participate in reference-count freeing).
   *
   * @param idx index, the `_asset` suffix can be used
   * @return pointer to asset, nullptr if not found
   */
  void* acquire(uint32_t idx);

  /**
   * Decrements an asset's reference count (pairs with acquire()).
   * When the count reaches zero the asset is freed, unless it is flagged
   * keep-loaded. Safe to call on an index that was never acquired (no-op).
   */
  void release(uint32_t idx);

  /**
   * Force-frees a single asset by index (mirrors freeAll() for one entry) and
   * resets its reference count to zero. Keep-loaded assets are left untouched.
   */
  void freeByIndex(uint32_t idx);

  const char* getPathByIndex(uint32_t idx);
}

namespace P64
{
  /**
   * Wrapper for assets to enable auto-loading and setting it in the editor.
   * This does not create any overhead in size, as a the pointer is used to
   * store either the index (if < 0xFFFF) or the actual pointer.
   *
   * The first call to get() will resolve the index to a pointer.
   * @tparam T asset type (e.g. sprite_t)
   */
  template<typename T>
  struct AssetRef
  {
    // direct pointer, only use if you know the asset is already loaded
    T* ptr{};

    /**
     * Getter that auto-loads the asset if needed.
     * @return pointer to the asset
     */
    T* get()
    {
      if((uint32_t)ptr < 0xFFFF) {
        ptr = (T*)AssetManager::getByIndex((uint32_t)ptr);
      }
      return ptr;
    }
  };

  struct PrefabRef
  {
    uint32_t idx;

    inline uint32_t get()
    {
      return idx;
    }
  };
}