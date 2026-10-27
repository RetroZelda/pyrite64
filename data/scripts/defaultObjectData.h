#pragma once
#include "script/userScript.h"

namespace P64::User
{
  struct __STRUCT__
  {
    // Other scripts can access this via 'obj->getScriptData<User::__STRUCT__>()'.
    // Methods can be added too, but no constructors, destructors or virtual functions.

    // Put your arguments and runtime values bound to an object here.
    // If you need them to show up in the editor, add a [[P64::Name("...")]] attribute.
    //
    // Types that can be set in the editor:
    // - uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t
    // - float
    // - bool (checkbox)
    // - color_t (color picker, default e.g. `color_t tint = {255, 0, 0, 255};`)
    // - fm_vec3_t, fm_quat_t
    // - AssetRef<sprite_t>
    // - ObjectRef
    //
    // For numbers you can add [[P64::Range(0, 10)]] to edit them as a slider,
    // or [[P64::Min(0)]] / [[P64::Max(10)]] to only clamp the entered value.
    //
    // For unsigned integers (uint8_t/uint16_t/uint32_t) you can add a
    // [[P64::Bitmask("0=Fire, 1=Water, 2=Earth")]] attribute to edit them as a
    // named multi-select of bits instead of a plain number.
    // [[P64::Enum("0=Off, 1=On")]] on integers shows a select-box with named values, and
    // [[P64::RequireComponent("Spline")]] on an ObjectRef only lists objects with that component.
    // Bitmask / Enum numbers can be decimal or 0x hex.
    // Put several attributes into one block: [[P64::Name("Mode"), P64::Enum("0=A, 1=B")]]
    //
    // [[P64::Group("Your Name")]] puts this and all following members into a group.
    // Until the next or an empty group ends it.
    //
    // Other types can be used but are not exposed in the editor.
  };
}
