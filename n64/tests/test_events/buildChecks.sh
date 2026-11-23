#!/usr/bin/env bash
# Event graph build checks: a binding that can never work must fail the build with a clear message.
# Each case copies this project to a temporary folder, edits one binding in scene 3 and builds it
# with the editor's CLI. Run from anywhere:
#   n64/tests/test_events/buildChecks.sh [path/to/pyrite64]
# N64_INST must point at the libdragon toolchain.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
EDITOR="${1:-$HERE/../../../pyrite64}"
EDITOR="$(cd "$(dirname "$EDITOR")" && pwd)/$(basename "$EDITOR")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

# case <title> <python expression editing scene 'd'> <text the build log must contain>
check() {
  local title="$1" edit="$2" expect="$3"
  local dir="$WORK/project"
  rm -rf "$dir"
  mkdir -p "$dir"
  cp -r "$HERE/project.p64proj" "$HERE/Makefile.custom" "$HERE/assets" "$HERE/data" "$HERE/src" "$dir/"
  rm -rf "$dir/src/p64" "$dir/assets/p64"
  python3 - "$dir/data/scenes/3/scene.json" "$edit" <<'PY'
import json, sys
path, edit = sys.argv[1], sys.argv[2]
d = json.load(open(path))
objs = {o['name']: o for o in d['graph']['children']}
door = objs['DoorA']['components'][0]['events']
exec(edit)
json.dump(d, open(path, 'w'), indent=2)
PY
  local log="$WORK/build.log"
  (cd "$(dirname "$EDITOR")" && "$EDITOR" --cli --cmd build "$dir/project.p64proj") > "$log" 2>&1
  local rc=$?
  if [ $rc -ne 0 ] && grep -qF "$expect" "$log"; then
    echo "OK   $title"
    pass=$((pass + 1))
  else
    echo "FAIL $title (exit $rc, expected: $expect)"
    grep -E 'events|ERR' "$log" | head -5
    fail=$((fail + 1))
  fi
}

check "malformed name" \
  "door['in']['Open'] = ['scene::Trigger Enter']" \
  "the name may only use letters"
check "global name without a system declaring it" \
  "door['out']['Opened'] = ['global::Nobody.Thing']" \
  "no global script declares the system 'Nobody'"
check "object input bound to a system input" \
  "door['in']['Open'] = ['global::EventTest.DoorOpened']" \
  "an object input cannot listen to it"
check "engine event bound to an output" \
  "door['out']['Opened'] = ['global::Engine.SceneLoaded']" \
  "is fired by the engine; an output cannot fire it"
check "undeclared namespace" \
  "door['in']['Open'] = ['quest::Started']" \
  "the namespace 'quest' is not declared"

echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
