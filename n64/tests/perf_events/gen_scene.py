#!/usr/bin/env python3
"""gen_scene.py : writes data/scenes/1/scene.json, the benchmark scene. Run it from this folder
after changing the numbers below (PerfTests.cpp has the same numbers)."""
import copy, json, pathlib

FAN = 64        # sinks bound to scene::P.Fan
CONDS_A = 64    # conditions with nothing bound, checked by scene::P.CheckA
CONDS_B = 32    # conditions with four outputs bound, checked by scene::P.CheckB
CONDS_C = 32    # conditions whose Result feeds the And nodes, checked by scene::P.CheckC
ANDS = 16       # And nodes, two conditions each
RELAYS = 8      # a chain of Relay objects
CONDS_L = 8     # the same as CONDS_C / ANDS with ids below 128: the engine finds those objects
ANDS_L = 4      # directly, objects with higher ids only by going through every object

def hx(s): return int(s, 16)
SINK, SOURCE, COND = hx("C5A1B2C3D4E5F620"), hx("C5A1B2C3D4E5F621"), hx("C5A1B2C3D4E5F622")
RELAY, AND = hx("C0E0000000000001"), hx("C0E0000000000010")

uid = [1000]
def nid():
    uid[0] += 1
    return uid[0]

def code(script, args=None, events=None):
    c = {"id": 0, "name": "Code", "uuid": nid(), "enabled": True, "data": {"args": args or {}, "script": script}}
    if events: c["events"] = events
    return c

def obj(name, comps):
    return {"children": [], "components": comps, "enabled": True, "name": name, "pos": [0.0, 0.0, 0.0],
            "propOverrides": {}, "proportionalScale": False, "rot": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0],
            "selectable": True, "uuid": nid(), "uuidPrefab": 0, "visMask": 1}

src = json.load(open("../test_events/data/scenes/5/scene.json"))
camera = copy.deepcopy(next(c for c in src["graph"]["children"] if c["name"] == "Camera"))
camera["uuid"] = nid()
for c in camera["components"]: c["uuid"] = nid()

children = [camera]
children.append(obj("Sink", [code(SINK, events={"in": {"Tick": ["scene::P.One"]}})]))
# first in the scene, so their ids are low
children += [obj(f"CondL{i}", [code(COND, events={"in": {"Check": ["scene::P.CheckL"]},
                                                  "out": {"Result": [f"scene::P.L{i}.Result"]}})]) for i in range(CONDS_L)]
children += [obj(f"AndL{j}", [code(AND, events={"in": {"Inputs": [f"scene::P.L{2 * j}.Result", f"scene::P.L{2 * j + 1}.Result"]},
                                                "out": {"NewlyTrue": ["scene::P.AndL.NewlyTrue"],
                                                        "NewlyFalse": ["scene::P.AndL.NewlyFalse"]}})]) for j in range(ANDS_L)]
children += [obj(f"SinkFan{i}", [code(SINK, events={"in": {"Tick": ["scene::P.Fan"]}})]) for i in range(FAN)]
children.append(obj("SinkOfSource", [code(SINK, events={"in": {"Tick": ["scene::P.FromSource"]}})]))
children.append(obj("SinkOfBusy", [code(SINK, events={"in": {"Tick": ["scene::P.FromBusy"]}})]))
children.append(obj("Source", [code(SOURCE, events={"out": {"Out": ["scene::P.FromSource"]}})]))
busy = {"Out": ["scene::P.FromBusy"]}
busy.update({f"O{i}": [f"scene::P.Dummy{i}"] for i in range(1, 16)})
children.append(obj("SourceBusy", [code(SOURCE, {"busy": "1"}, {"out": busy})]))
children.append(obj("SendTarget", [code(SINK, {"sendTarget": "1"})]))
children.append(obj("CodeTarget", [code(COND, {"codeTarget": "1"})]))
children += [obj(f"CondA{i}", [code(COND, events={"in": {"Check": ["scene::P.CheckA"]}})]) for i in range(CONDS_A)]
children += [obj(f"CondB{i}", [code(COND, events={"in": {"Check": ["scene::P.CheckB"]}, "out": {
    "True": ["scene::P.B.True"], "False": ["scene::P.B.False"],
    "NewlyTrue": ["scene::P.B.NewlyTrue"], "NewlyFalse": ["scene::P.B.NewlyFalse"]}})]) for i in range(CONDS_B)]
children += [obj(f"CondC{i}", [code(COND, events={"in": {"Check": ["scene::P.CheckC"]},
                                                  "out": {"Result": [f"scene::P.C{i}.Result"]}})]) for i in range(CONDS_C)]
children += [obj(f"And{j}", [code(AND, events={"in": {"Inputs": [f"scene::P.C{2 * j}.Result", f"scene::P.C{2 * j + 1}.Result"]},
                                               "out": {"NewlyTrue": ["scene::P.And.NewlyTrue"],
                                                       "NewlyFalse": ["scene::P.And.NewlyFalse"]}})]) for j in range(ANDS)]
children += [obj(f"Relay{k}", [code(RELAY, events={"in": {"Trigger": [f"scene::P.Chain{k}"]},
                                                   "out": {"Fired": [f"scene::P.Chain{k + 1}"]}})]) for k in range(RELAYS)]

# last in the scene: an id above 128
children.append(obj("SendTargetFar", [code(SINK, {"sendTarget": "2"})]))

scene = {"conf": copy.deepcopy(src["conf"]), "version": src["version"]}
scene["conf"]["name"] = "benchmark"
root = copy.deepcopy(src["graph"]); root["children"] = children; root["uuid"] = nid()
scene["graph"] = root
pathlib.Path("data/scenes/1").mkdir(parents=True, exist_ok=True)
pathlib.Path("data/scenes/1/scene.json").write_text(json.dumps(scene, indent=2))
print(len(children), "objects; ids:", {c["name"]: i + 1 for i, c in enumerate(children) if c["name"] in ("SendTarget", "SendTargetFar", "CondL0", "AndL0", "CondA0", "CondC0", "And0")})
