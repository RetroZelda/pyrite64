/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "skeleton.h"
#include "../project/assets/model3d.h"

Renderer::Skeleton::Skeleton(SDL_GPUDevice* device, const Project::Assets::Model3D &model)
  : storageBuff{device}
{
}
