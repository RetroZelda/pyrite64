/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>

namespace Editor
{
  class LogWindow
  {
    private:
      uint32_t lastBuildLineCount{0};

    public:
      void draw();
      void drawBuild();
  };
}