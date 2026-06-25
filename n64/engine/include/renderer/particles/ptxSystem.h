/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#pragma once
#include <t3d/t3d.h>
#include <t3d/tpx.h>

namespace P64::PTX
{
  class System
  {
    public:
      enum Type : uint8_t
      {
        COLOR_RGBA_S8,
        TEX_RGBA_S8,
        COLOR_A_S16,
        TEX_A_S16,
      };

    fm_vec3_t pos{};
    void *particles{};
    uint32_t countMax{};
    uint32_t count{};

    const Type type;

    // Optional local->world transform pushed around draw() via the tpx matrix stack.
    // When set, particle positions are interpreted in LOCAL space (cheap to "move" the
    // whole emitter by just updating this matrix). nullptr => positions are world-space
    // (the original behaviour, used by PTX::Sprites/jam25 — unchanged).
    T3DMat4FP *mat{nullptr};

    // bytes per particle for this type (S8 pair = 16B, S16 pair = 24B)
    [[nodiscard]] uint32_t strideBytes() const {
      return (type == COLOR_A_S16 || type == TEX_A_S16) ? (sizeof(TPXParticleS16) / 2)
                                                        : (sizeof(TPXParticleS8) / 2);
    }

    System(Type ptxType, uint32_t maxSize = 0);
    ~System();

    TPXParticleS8* getBufferS8() const {
      return (TPXParticleS8*)particles;
    }
    TPXParticleS16* getBufferS16() const {
      return (TPXParticleS16*)particles;
    }

    [[nodiscard]] bool isFull() const { return count == countMax; }

    void removeParticle(uint32_t index) {
      if(type == COLOR_A_S16 || type == TEX_A_S16)
      {
        tpx_buffer_s16_copy(getBufferS16(), index, --count);
        if(count & 1) {
          *tpx_buffer_s16_get_size(getBufferS16(), count + 1u) = 0;
        }
      } else {
        tpx_buffer_s8_copy(getBufferS8(), index, --count);
        if(count & 1) {
          *tpx_buffer_s8_get_size(getBufferS8(), count + 1u) = 0;
        }
      }
    }

    void draw() const;
  };
}