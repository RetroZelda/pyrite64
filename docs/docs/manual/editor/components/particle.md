# Particle

Places a particle emitter on an object.\
The emitter is an `.emitter` asset, created and edited in the **Particles** tab of the asset browser:
its shape, speed, size and color over life, texture and blending are set in the Asset panel when the emitter is selected.

The component owns its emitter: it exists as long as the object does,
follows the object's position every frame and emits along the object's facing.

## Options

| Option | Description |
|--------|-------------|
| **Emitter** | The emitter asset to play. |
| **Auto-Play** | Starts emitting when the object spawns.<br>Without it, the emitter waits until the object receives the `EVENT_TYPE_PARTICLE_PLAY` event, so a script can start several emitters together. |
| **Force-Destroy** | When the object is deleted, drop the emitter and its live particles immediately.<br>Off (default): the emitter stops spawning and lets its live particles finish first. |

## Units and facing

All lengths of an emitter are meters: the shape size and offset, the speeds (m/s) and the gravity (m/s²).\
Particle sizes are meters as well, so a particle keeps its size in scenes with a different render scale.

Directional emission is turned about the Y axis to the object's facing, which is **-Z** forward
(the blue axis of the viewport's facing gizmo).\
With **Sim Space** set to **Relative to Parent**, spawned particles follow the object's parent instead of the object itself
(without a parent they behave like **World Space**).

The viewport previews the emitter with the same simulation as the ROM, but does not apply the object's rotation yet.

## Runtime API

```cpp
#include "scene/components/particle.h"

auto *ptx = obj.getComponent<P64::Comp::Particle>();
if(ptx && ptx->handle.isValid())
{
  ptx->handle.stop();   // stop spawning, the live particles finish
  ptx->handle.replay(); // restart the emission from scratch
}

// start the emitters of an object that don't auto-play
obj.getScene().sendEvent(obj.id, obj.id, P64::EVENT_TYPE_PARTICLE_PLAY, 0);
```

Emitters can also be played without a component, e.g. for one-shot effects:
`P64::EmitterManager::play()` starts one that cleans itself up once finished,
while `P64::EmitterManager::create()` returns a handle the caller has to `destroy()` again.
