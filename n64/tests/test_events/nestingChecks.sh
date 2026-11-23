#!/usr/bin/env bash
# Event graph, phase 4: "To Prefab" and "Unpack" must keep every binding reaching what it reached.
# Copies this project to a temporary folder, wraps scene 7's Group into a prefab with the editor's
# CLI, checks the rewritten names, builds and runs the ROM in ares, then unpacks the Group again
# and does the same. Test 11 ("Same events after To Prefab and Unpack") must pass every time.
#   n64/tests/test_events/nestingChecks.sh [path/to/pyrite64]
# N64_INST must point at the libdragon toolchain, and ares must be on the PATH.
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

pass=0
fail=0
ok()  { echo "OK   $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

editor() { (cd "$(dirname "$EDITOR")" && "$EDITOR" --cli "$@" "$DIR/project.p64proj") > "$WORK/editor.log" 2>&1; }

# build and run the ROM, then look for test 11
run() {
  local title="$1"
  if ! editor --cmd build; then
    bad "$title: build"
    grep -E 'events|ERR' "$WORK/editor.log" | head -5
    return
  fi
  local log="$WORK/ares.log"
  rm -f "$log"
  timeout 120 ares --no-file-prompt --setting Audio/Mute=true --setting Input/Defocus=Allow \
    --system "Nintendo 64" "$DIR/test_events.z64" > "$log" 2>&1 &
  local pid=$!
  for _ in $(seq 1 100); do
    grep -q '\[event test\] DONE' "$log" 2>/dev/null && break
    sleep 1
  done
  # only this run's emulator: timeout passes the signal on (the emulator runs as ares-simd)
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
  # every test passed, however many there are
  if grep -q '\[event test\] 11 Same events after To Prefab and Unpack: OK' "$log" \
     && grep -qE '\[event test\] DONE: ([0-9]+) of \1 passed' "$log"; then
    ok "$title: runtime"
  else
    bad "$title: runtime"
    grep '\[event test\]' "$log" | grep -v ': OK' | head -20
  fi
}

# check <title> <python condition over the scene 's7' and, after wrapping, the prefab 'pf'>
check() {
  if python3 - "$DIR" "$2" <<'PY'
import json, sys, glob
d, cond = sys.argv[1], sys.argv[2]
s7 = json.load(open(d + '/data/scenes/7/scene.json'))
top = {o['name']: o for o in s7['graph']['children']}
group = top['Group']
files = glob.glob(d + '/assets/prefabs/Group.prefab')
pf = json.load(open(files[0]))['obj'] if files else None
def child(o, name): return [c for c in o['children'] if c['name'] == name][0]
def events(o): return o['components'][0].get('events', {})
def link(o, name):
    return [x['to'] for x in o.get('eventOverrides', []) if x.get('link') == name]
def adds(o): return sorted(n for x in o.get('eventOverrides', []) for n in x.get('add', []))
sys.exit(0 if eval(cond) else 1)
PY
  then ok "$1"; else bad "$1"; fi
}

run "as authored"

if editor --cmd to-prefab --scene 7 --object Group --dir prefabs; then ok "to-prefab"; else bad "to-prefab"; cat "$WORK/editor.log"; fi
check "wrapped: the group is an instance" "group['uuidPrefab'] != 0 and not group['children'] and pf is not None"
check "wrapped: the alarm only the group used became the prefab's own" \
  "events(child(pf, 'Horn'))['out']['Raised'] == ['prefab::Alarm7'] and link(child(pf, 'Kit'), 'prefab_1::Alarm') == ['prefab::Alarm7']"
check "wrapped: names shared with code or the watcher are kept" \
  "events(child(pf, 'Horn'))['in']['Trigger'] == ['scene::Sound7'] and adds(child(pf, 'Kit')) == ['scene::Kit7Opened', 'scene::Press7']"
run "wrapped"

if editor --cmd unpack --scene 7 --object Group; then ok "unpack"; else bad "unpack"; cat "$WORK/editor.log"; fi
check "unpacked: plain objects again" "group['uuidPrefab'] == 0 and sorted(c['name'] for c in group['children']) == ['Horn', 'Kit']"
check "unpacked: the alarm is a scene event again" \
  "events(child(group, 'Horn'))['out']['Raised'] == ['scene::Alarm7'] and link(child(group, 'Kit'), 'prefab_1::Alarm') == ['scene::Alarm7']"
check "unpacked: the kit keeps its overrides" "adds(child(group, 'Kit')) == ['scene::Kit7Opened', 'scene::Press7']"
run "unpacked"

echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
