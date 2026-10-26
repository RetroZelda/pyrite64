# Code

```{image} /_static/img/ui_comp_code.png
:align: center
```

Attaches a C++ script to the object, giving it custom behavior.\
An object can have several Code components to compose behavior from multiple small scripts.\
For how scripts are written, see the {doc}`Object-Scripts <../../script>` guide.

## Options

| Option | Description |
|--------|-------------|
| **Script** | The script asset to attach. You can also drag a script from the file list onto this field. |
| **Arguments** | Any values the script exposes to the editor show up here, with an editor for each (number field, select-box, asset/object picker). A field is exposed by adding a `[[P64::Name("...")]]` attribute to a member of the script's `P64_DATA` block (see {doc}`Object-Scripts <../../script/objScript>`, including the `[[P64::Bitmask(...)]]` attribute for named bit flags, `[[P64::Enum(...)]]` for named values and `[[P64::RequireComponent(...)]]` to limit an object picker to objects with a given component). |

## Arguments

- `P64::Enum` and `P64::Bitmask` values can be written in decimal or in hex with a `0x` prefix.\
  An entry that can't be read, or that the member can't hold, is skipped with a warning in the log.
- An object picker lists the objects placed directly in the scene.\
  With `P64::RequireComponent` it only lists objects with that component; an unknown component name\
  (it is case-sensitive, e.g. `"spline"` instead of `"Spline"`) shows a warning below the picker, and all objects are listed.
- An object reference is `0` inside prefabs and for spawned objects,\
  see {ref}`Object references and prefabs <objscript-objectref-prefabs>`.

On a prefab instance, each argument has a lock button to override its value for that instance only.\
An argument whose script member has the same name as another property of the object (e.g. `scale` of its transform)\
can't be overridden while that property is overridden on the instance: rename the member in the script,\
see {ref}`Prefab Instance Overrides <objscript-instance-overrides>`.

## See also

- {doc}`Object-Scripts <../../script>`: writing and exposing values from scripts.
- {cpp:struct}`P64::Comp::Code`: the runtime component in the C++ API.
