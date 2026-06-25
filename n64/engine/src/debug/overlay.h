/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>

namespace P64::Debug { struct Menu; }

namespace P64::Debug::Overlay
{
  extern uint64_t ticksSelf;
  extern bool useCpuAvg;
  extern bool useDetailedTotals;

  void init();
  void draw(surface_t* surf);

  // sub overlays:
  void ovlAudio();
  void ovlMemory();
  void ovlCPU();
  void ovlComponents();
  void ovlScripts();
  void ovlUserGlobals();
  void ovlParticles();

  // Particles page: populate `m` with one toggle per emitter slot, and (separately) draw the
  // emission shapes/gizmos for slots whose toggle is on. (defined in overlay/ovlParticles.cpp)
  void buildParticlesMenu(Menu& m);
  void drawParticleShapes();
}
