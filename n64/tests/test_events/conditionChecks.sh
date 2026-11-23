#!/usr/bin/env bash
# Conditions (docs/todo/conditionals/PLAN.md): what the editor says about scene 10, and wiring a
# condition into a logic node with the editor's CLI (the functions the Events window calls). Works
# on a copy of the project. Run from anywhere:
#   n64/tests/test_events/conditionChecks.sh [path/to/pyrite64]
# N64_INST must point at the libdragon toolchain (the last checks build the copy).
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
SCENE="$DIR/data/scenes/10/scene.json"
LOG="$WORK/editor.log"

pass=0
fail=0
ok()  { echo "OK   $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

editor() { (cd "$(dirname "$EDITOR")" && "$EDITOR" --cli "$@" "$DIR/project.p64proj") > "$LOG" 2>&1; }

# has <title> <text the last editor output must contain>
has() { if grep -qF -- "$2" "$LOG"; then ok "$1"; else bad "$1 (missing: $2)"; fi; }
# lacks <title> <text the last editor output must not contain>
lacks() { if grep -qF -- "$2" "$LOG"; then bad "$1 (found: $2)"; else ok "$1"; fi; }

# check <title> <python condition over the objects of scene 10 by name, 'o'>
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

# 1. the graph as text
editor --cmd graph --scene 10 || { cat "$LOG"; bad "graph of scene 10"; }
has "a condition is marked, with its static values" "node CondA (condition) [Slot: 0]"
has "a condition that starts disabled says so" "node Gated (condition) [Starts disabled, Slot: 3]"
has "a logic node is a built-in condition" "node And (built-in) (condition)"
has "the script's trigger shows on Check" "in  CondA|TestCond.Check: global::Test.CondTick (trigger, from the script)"
has "a trigger is a wire from the Global node" "wire [Global] -> CondA|TestCond.Check : global::Test.CondTick"
has "a Result wired into a logic node" "wire CondA|TestCond.Result -> And|ConditionAnd.Inputs : scene::C.A.Result"
has "a logic node wired into a logic node" "wire And|ConditionAnd.Result -> Nand|ConditionNot.Inputs : scene::C.And.Result"
lacks "a condition without triggers has none" "CondM|TestCondManual.Check"
has "a binding written on Check stays beside the trigger" "in  Gated|TestCond.Check: scene::C.Kick global::Test.CondTick (trigger, from the script)"
has "Auto Check off shows on the node" "node Quiet (condition) [Auto Check off, Slot: 0]"
lacks "and leaves the trigger out" "Quiet|TestCond.Check"

# 2. wiring: a new wire reuses the Result's name; a second logic node reads the same condition
editor --cmd wire --scene 10 --from 'CondM|TestCondManual.Result' --to 'Orphan|ConditionAnd.Inputs' || { cat "$LOG"; bad "wire M into Orphan"; }
check "the wire bound Inputs to M's Result" "'scene::C.M.Result' in ev('Orphan')['in']['Inputs']"
check "M's Result kept its one name" "ev('CondM')['out']['Result'] == ['scene::C.M.Result']"

# a Result that is not bound yet gets a name of its own
editor --cmd wire --scene 10 --from 'Xor|ConditionXor.Result' --to 'Orphan|ConditionAnd.Inputs' || { cat "$LOG"; bad "wire Xor into Orphan"; }
check "an unbound Result is named after its object" "ev('Xor')['out']['Result'] == ['scene::Xor.Result']"
check "and the logic node reads it" "'scene::Xor.Result' in ev('Orphan')['in']['Inputs']"

# removing the wire: the input stops reading, the Result stops firing a name nothing else uses
editor --cmd unwire --scene 10 --from 'Xor|ConditionXor.Result' --to 'Orphan|ConditionAnd.Inputs' || { cat "$LOG"; bad "unwire Xor from Orphan"; }
check "the removed wire left no binding on Inputs" "'scene::Xor.Result' not in ev('Orphan')['in']['Inputs']"
check "and none on the Result" "'Result' not in ev('Xor').get('out', {}) or ev('Xor')['out']['Result'] == []"

# a trigger is set in the script: the graph cannot remove it
editor --cmd unwire --scene 10 --to 'CondA|TestCond.Check' --event global::Test.CondTick
editor --cmd graph --scene 10
has "a trigger cannot be removed in the graph" "in  CondA|TestCond.Check: global::Test.CondTick (trigger, from the script)"

# 3. the build: the input that reads a name nothing provides is reported once, as a condition
editor --cmd build || { grep -E 'ERR|error' "$LOG" | head; bad "build"; }
has "an input reading a name without a condition is reported" "Orphan / ConditionAnd.Inputs reads 'scene::C.Missing.Result', but no condition's Result is bound to it; it counts as false"
lacks "and only once" "ConditionAnd.Inputs listens to 'scene::C.Missing.Result'"
lacks "an input reading a real Result is fine" "reads 'scene::C.M.Result'"
lacks "a trigger the game fires is not reported" "listens to 'global::Test.CondTick'"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
