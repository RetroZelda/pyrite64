#!/usr/bin/env bash
# Event graph, phase 5: scene 8 is wired only by the Events window's operations. This copies the
# project, removes every binding from scene 8, draws the same wires again with the editor's CLI
# (the functions the window calls when a wire is dragged) and requires the file to come out
# exactly as committed: wiring adds binding lines and nothing else. Removing wires is checked too.
#   n64/tests/test_events/graphChecks.sh [path/to/pyrite64]
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
EDITOR="${1:-$HERE/../../../pyrite64}"
EDITOR="$(cd "$(dirname "$EDITOR")" && pwd)/$(basename "$EDITOR")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

DIR="$WORK/project"
mkdir -p "$DIR"
cp -r "$HERE/project.p64proj" "$HERE/Makefile.custom" "$HERE/assets" "$HERE/data" "$HERE/src" "$DIR/"
rm -rf "$DIR/src/p64" "$DIR/assets/p64"
SCENE="$DIR/data/scenes/8/scene.json"

pass=0
fail=0
ok()  { echo "OK   $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

editor() { (cd "$(dirname "$EDITOR")" && "$EDITOR" --cli "$@" "$DIR/project.p64proj") > "$WORK/editor.log" 2>&1; }
wire()   { editor --cmd wire --scene 8 "$@" || { cat "$WORK/editor.log"; bad "wire $*"; }; }
unwire() { editor --cmd unwire --scene 8 "$@" || { cat "$WORK/editor.log"; bad "unwire $*"; }; }

# check <title> <python condition over the objects of scene 8 by name, 'o'>
check() {
  if python3 - "$SCENE" "$2" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
o = {x['name']: x for x in d['graph']['children']}
def ev(name, i=0): return o[name]['components'][i].get('events', {})
sys.exit(0 if eval(sys.argv[2]) else 1)
PY
  then ok "$1"; else bad "$1"; fi
}

wire_all() {
  wire --from 'Driver|TestDriver8.Go' --to 'Relay|Relay.Trigger'
  wire --from 'Relay|Relay.Fired' --to 'Door|TestDoor.Open'
  wire --from 'Door|TestDoor.Opened' --event global::EventTest.DoorOpened
  wire --from 'Driver|TestDriver8.Shut' --to 'Door|TestDoor.Close'
  wire --focus Door --from 'Door|TestDoor.Closed' --to 'Door|TestLamp.Light'
  wire --to 'Door|TestDoor.Knock' --event scene::Loaded
}

# 1. without its bindings, scene 8 differs from the committed file only in binding lines
python3 - "$SCENE" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
def strip(o):
    o.pop('events', None); o.pop('eventOverrides', None)
    for c in o.get('components', []): c.pop('events', None)
    for c in o.get('children', []): strip(c)
strip(d['graph'])
json.dump(d, open(sys.argv[1], 'w'), indent=2)
PY
wire_all
if diff -u "$HERE/data/scenes/8/scene.json" "$SCENE" > "$WORK/diff.txt"; then
  ok "wiring an unbound scene 8 gives the committed file, byte for byte"
else
  bad "wiring an unbound scene 8 gives the committed file"
  head -40 "$WORK/diff.txt"
fi

# 2. removing a wire: the input stops listening, and the output stops firing the event the
#    graph made for it, since nothing else uses it
unwire --from 'Relay|Relay.Fired' --to 'Door|TestDoor.Open'
check "unwired: the door no longer listens" "'Open' not in ev('Door').get('in', {})"
check "unwired: the relay no longer fires the event made for the wire" "'Fired' not in ev('Relay').get('out', {})"
check "unwired: other wires stay" "ev('Door')['in']['Close'] == ['scene::Driver.Shut'] and ev('Relay')['in']['Trigger'] == ['scene::Driver.Go']"

# 3. an output that still has a listener keeps firing
wire --from 'Driver|TestDriver8.Go' --to 'Door|TestDoor.Open'
unwire --from 'Driver|TestDriver8.Go' --to 'Door|TestDoor.Open'
check "unwired one of two listeners: the output keeps its event" "ev('Driver')['out']['Go'] == ['scene::Driver.Go'] and 'Open' not in ev('Door').get('in', {})"

# 4. wires to the Global node and from the scope node come off as well
unwire --from 'Door|TestDoor.Opened' --event global::EventTest.DoorOpened
unwire --to 'Door|TestDoor.Knock' --event scene::Loaded
check "unwired the Global and scope wires" "'Opened' not in ev('Door').get('out', {}) and 'Knock' not in ev('Door').get('in', {})"

# 5. wiring again restores the committed file
wire --from 'Relay|Relay.Fired' --to 'Door|TestDoor.Open'
wire --from 'Door|TestDoor.Opened' --event global::EventTest.DoorOpened
wire --to 'Door|TestDoor.Knock' --event scene::Loaded
if diff -q "$HERE/data/scenes/8/scene.json" "$SCENE" > /dev/null; then
  ok "wired again: the committed file"
else
  bad "wired again: the committed file"
  diff -u "$HERE/data/scenes/8/scene.json" "$SCENE" | head -30
fi

echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
