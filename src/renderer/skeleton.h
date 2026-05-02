/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "storageBuffer.h"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Project::Assets
{
  struct Model3D;
}

namespace Renderer
{
  class Skeleton
  {
    public:
      struct Bone
      {
        glm::mat4 mat{};
        glm::vec3 pos{};
        glm::quat rot{};
        glm::vec3 scale{};
        uint32_t parentIdx{};
      };

    private:
      StorageBuffer storageBuff;
      std::vector<Bone> bones{};

    public:
      explicit Skeleton(
        SDL_GPUDevice* device,
        const Project::Assets::Model3D &model
      );
  };
}
