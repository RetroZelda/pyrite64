/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "glm/vec3.hpp"

namespace Editor
{
  class SceneGraph
  {
    private:

    public:
//      SceneGraph();
//      ~SceneGraph();

      // viewCenter: centre of the active 3D viewport, where objects added to the scene root
      // start when they have no position of their own ("Add Spline Path")
      void draw(const glm::vec3 &viewCenter = glm::vec3{0.0f});
  };
}
