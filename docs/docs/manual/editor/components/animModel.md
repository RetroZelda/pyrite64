# Model (Animated)

```{image} /_static/img/ui_comp_model_anim.png
:align: center
```

Renders an animated (skeletal) 3D model. Works like the {doc}`static model <model>`
component, but the model can play skinned animations at runtime.

## Options

| Option | Description |
|--------|-------------|
| **Model** | The animated 3D model asset to render. |
| **Open Model Editor** | Opens the model editor for the selected asset. |
| **Draw-Layer** | Which 3D draw layer the model is rendered on (see scene settings). |
| **Blend Skeletons** | On (the default): the engine keeps a copy of the skeleton for every clip, so the model can blend two clips (PlayBlend, `setBlendAnim()`). Off: only the one skeleton that is drawn exists, which saves a skeleton per clip (54 bones × 27 clips is about 140 KB), and blend requests are ignored with one warning. Turn it off for characters that only ever play one clip at a time. |
| **Preview Anim.** | Selects an animation to preview inside the editor viewport. This is an editor-only aid and does not affect what plays at runtime. |
| **Material Instance** | Per-object material overrides for this model. See {doc}`Material Instance <../materials/instance>` for the full list of options. |

## Event pins

In the event graph the component has the inputs **PlayMain** and **PlayBlend** (the value is the clip
index) and the output **MainFinished** (a non-looping main animation stopped; the value is its clip
index).

It also gets one output per **animation event** its model lists in the project's event catalog, in
any `events/*.json` file:

```json
{
  "animationEvents": {
    "assets/hero.glb": {
      "doc": "the hero",
      "events": [
        {"name": "Run.STEP_LEFT", "doc": "Run, at 0.65 s", "value": "the clip index"},
        {"name": "Any.LAND", "doc": "LAND of any clip"}
      ]
    }
  }
}
```

The key is the model's path in the project. A name uses letters, digits, `_` and dots, at most 64 per
model. The engine never decides when they fire: game code does, by name, usually from its animation
code:

```cpp
P64::Comp::AnimModel::fireEvent(obj, anim, P64::Events::PinName{P64::Events::pinHash("Run.STEP_LEFT", 13)}, clip);
```

`fireEvent()` returns false when nothing can hear it (the object has no bindings, or the model does
not list the name). After `swapModel()` the pins follow the model shown.

## See also

- {doc}`Material Instance <../materials/instance>`: the embedded material sub-UI.
- {cpp:struct}`P64::Comp::AnimModel`: the runtime component in the C++ API.
