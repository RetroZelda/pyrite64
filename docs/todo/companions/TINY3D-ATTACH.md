# tiny3d: making `t3d_anim_attach` cheap

**Status (2026-11-27): done.**

- RetroZelda/tiny3d `full-frontal` is at **95bc862**.
- pyrite64 `stream/full-frontal` **65ee072** points its submodule there.
- The local toolchain has it installed.
- [Section 8](#8-outcome-2026-11-27) has the results and the fork's branch setup: `full-frontal` is the
  default branch, and `main` mirrors upstream.

It is decision 33 in [PLANNING-STATE.md](PLANNING-STATE.md).

## Summary

- **The problem:** attaching an animation clip to a skeleton costs about 10 ms of N64 CPU time for a
  RetroStar clip. Almost all of that is one loop in the fork's own code, which recounts every channel
  after each keyframe it reads.
- **The fix:** count each channel once, on the keyframe that fills it. It is about ten lines in one
  file (`src/t3d/t3danim.c`).
- **The result:** the same keyframes are read and the bones get the same values. The checks drop from
  about 42,000 to about 300 per attach. Measured in ares, a RetroStar attach went from 9.5–12 ms to
  1.4–2.2 ms.
- **Who is affected:** no API, ABI, header, memory or asset-format change. Everything that links
  `libt3d.a` gets faster once it is rebuilt. Nothing else has to change.

## 1. What is slow, and why

`t3d_anim_attach(anim, skeleton)` connects a clip's channels to a skeleton's bones. A channel is one
bone's rotation, or one axis of its position or scale. It does four things:

1. Allocates the per-channel targets and points each one at its bone.
2. **Primes** every channel: it reads keyframes from the clip's stream until every channel has one.
   This is the fork-only part.
3. Copies each channel's first value into its starting value.
4. Rewinds the stream.

Step 2 is this loop (`src/t3d/t3danim.c`, fork commit 63bafbb):

```c
uint32_t initCount = 0;
while(initCount < channelCount) {
  if(!load_keyframe(anim))break;
  initCount = 0;                                   // recount every channel...
  for(int c = 0; c < anim->animRef->channelsQuat; c++) {
    if(anim->targetsQuat[c].base.timeEnd > 0)initCount++;
  }
  for(int c = 0; c < anim->animRef->channelsScalar; c++) {
    if(anim->targetsScalar[c].base.timeEnd > 0)initCount++;
  }                                                // ...after every single keyframe
}
```

**Why it is quadratic.** The glTF importer writes each clip's keyframes in the order the player needs
them (`tools/gltf_importer/src/converter/animConverter.cpp`, the sort by `timeNeeded`). For each
channel, that order looks like this:

- Its first two keyframes are both needed at time 0, so the stream starts with every channel's first
  keyframe and then every channel's second.
- A channel's first keyframe has 0 as its "time to the next keyframe", so it leaves the channel's end
  time at 0. Only the second keyframe moves it off 0.

So the loop reads exactly 2 × C keyframes, where C is the number of channels. After each one it checks
all C channels, which makes 2 × C² checks per attach.

I replayed the loop on the built clips (`filesystem/*.sdata`, with channel counts from the `.t3dm`
files):

| Model | Channels per clip | Keyframes read | Checks now | Checks after the fix | Measured attach (ares) |
|---|---|---|---|---|---|
| RetroStar (27 clips) | 138–152 | 2 × C (276–304) | 38,088–46,208 | 276–304 | 9.5–12.0 ms |
| Minion (5 clips) | 20 | 40 | 800 | 40 | 0.41 ms (most likely these) |

The timings come from `[PROF]` lines around the attach call in a scratch build, run in ares. The two
0.41 ms timings are not labelled; their size matches a 20-channel clip.

**Where it hurts.** It costs about 10 ms at 30 fps, a third of a frame, as a single hitch.

- **Companions and enemies:** with the engine's new attachment tracking, the first time each clip plays
  on each model. Two companions starting to walk make two 10 ms hitches in the same moment.
- **The Player (same RetroStar model):** every blend. A blend attaches the incoming clip to its own
  blend skeleton (`AnimModel::setBlendAnim`). When that clip takes over, it is attached again to the main
  skeleton. Each move between skeletons is a full attach, which the engine's tracking cannot avoid. So
  every blended state change costs her 10–20 ms today.
- **Later:** any model with a big rig.

## 2. Where the loop came from

The upstream library (HailToDodongo/tiny3d) has no priming. The loop is the fork's own fix for bones
that pop (zero scale or rotation) in the first frames after a clip is attached:

| Commit | When | What |
|---|---|---|
| `a4638a9` (branch `fix/jank-animation`) | 2026-05-09 | First version, on the old file-based keyframe loader |
| `63bafbb` (branch `full-frontal`) | 2026-10-27 | Port to the streaming loader (upstream #84), the code above |

The fix keeps what priming does and only changes how it counts.

## 3. The change

All of it is in `src/t3d/t3danim.c`.

1. Make `load_keyframe` return the channel it loaded into, or `NULL` at the end of the stream, instead
   of `true` / `false`. It already has that pointer (`targetBase`). The one caller in the hot path,
   `t3d_anim_update`, only tests it (`if(!load_keyframe(anim))break;`), so that line does not change.
2. In the priming loop, count only the channel just loaded, and only on the keyframe that moves its end
   time off 0.

```c
// forward declaration and definition: bool -> T3DAnimTargetBase*
static inline T3DAnimTargetBase* load_keyframe(T3DAnim *anim) {
  T3DAnimKF kf __attribute__((aligned(8), uninitialized));
  if(!stream_read_kf(anim, &kf))return NULL;
  ...                                              // unchanged
  return targetBase;
}

// t3d_anim_attach, the priming loop:
// A channel counts once, on the keyframe that moves its end time off zero. load_keyframe copies the
// old end time into timeStart, so that keyframe is the one that leaves timeStart == 0 and timeEnd > 0.
uint32_t initCount = 0;
while(initCount < channelCount) {
  T3DAnimTargetBase *t = load_keyframe(anim);
  if(!t)break;
  if(t->timeStart == 0.0f && t->timeEnd > 0.0f)initCount++;
}
// the kfCurr copy and rewind_anim(anim) below stay as they are
```

**Why the result is identical.**

- `load_keyframe` sets `timeStart = old timeEnd`, then adds the keyframe's time to `timeEnd`. Only when
  that time is 0 does it also nudge `timeStart` down by 0.00001.
- So a load moves a channel's end time off 0 exactly when the old end time was 0 and the added time is
  above 0. In that case `timeStart` is exactly `0.0f`. In every other case `timeStart` is not 0 or
  `timeEnd` is still 0.
- End times never return to 0 during priming. So the running count equals the old full recount after
  every keyframe.
- The loop therefore stops after the same keyframe: the same stream position, the same targets, the
  same starting values and the same rewind.

**What stays.** The work that grows only with C:

- the 2 × C keyframe reads, about 2 KB of stream for a RetroStar clip;
- the quaternion unpacking;
- the DMA waits at buffer-half boundaries;
- the blocking reload in `rewind_anim`.

**Expected cost.** Fitting the two measured sizes (20 and about 145 channels) gives about 0.19 µs per
check (18 CPU cycles), with at most 13 µs per channel for the rest. So a RetroStar attach should drop
from about 10 ms to between 0.5 and 2 ms. The range depends on how much of the Minion's 0.41 ms is a
fixed cost. This is an estimate: section 6 measures it.

**Alternatives I rejected:**

- **Remove priming:** brings back the pop the fork fixed in May.
- **Read exactly 2 × C keyframes:** relies on how the importer happens to order keyframes, and would
  break for channels with one keyframe or other layouts.
- **A bitmap of filled channels:** an extra allocation per attach for the same result.
- **Cache primed clips in the engine:** the engine already skips attaches on the same skeleton, which
  is the part it can avoid. A move between skeletons needs a real attach.

## 4. What it affects

| System | How it gets tiny3d | Effect | Action |
|---|---|---|---|
| Kentucky Castle ROM | links `libt3d.a` from the toolchain (`include $(N64_INST)/include/t3d.mk`) | faster attaches, same poses | rebuild |
| Local toolchain (`~/.local/n64`) | built from `~/.local/src/tiny3d` (`full-frontal` at 63bafbb, installed 2026-11-02) | used by `make`, the editor's builds and the scratch builds | `make && make install` in `~/.local/src/tiny3d` |
| KC CI (`build-rom.yml`) | resolves the head of `full-frontal` on every run and builds it | picks the change up on the next run, PRs included | none. The first run misses the toolchain cache (keyed on the tiny3d SHA) and rebuilds libdragon and tiny3d once, so it takes longer |
| pyrite64 editor | compiles only the glTF importer from `vendored/tiny3d` (submodule pinned at 63bafbb) | none: `t3danim.c` is not part of the editor | optional: bump the submodule so the pin matches the runtime |
| pyrite64 engine (`AnimModel`) | runs on the N64, links the same `libt3d.a` | its attachment tracking stays useful: a rewind is still cheaper than an attach | none |
| pyrite64 Windows setup (`data/scripts/mingw_create_env.sh`) | clones `full-frontal` and builds it | new installs get it. Existing installs skip the build unless `FORCE_UPDATE=true` | none, or tell users to re-run with `FORCE_UPDATE=true` |
| Editor SDK check (`src/utils/toolchain.cpp`) | looks for symbols in `t3d.h` | `t3d.h` does not change, so it will not report old installs as out of date | optional: raise `T3D_LIB_VER_PATCH` to 1. The check matches `T3D_LIB_VER_MAJOR 1`, which stays |
| Assets (`.t3dm`, `.sdata`) | written by the importer | format unchanged, so nothing to reconvert | none |
| Public API and ABI | `t3danim.h` | no change: `load_keyframe` is `static`, and no struct changes | none |
| Upstream tiny3d | none | not affected: priming exists only in the fork | optional: offer the priming and this fix upstream as one PR |
| Other projects on this machine | none | I searched `~/Development` and `~/.local/src`: only pyrite64 and Kentucky Castle use tiny3d | none |

One thing to keep in mind regardless of this change: KC's CI builds the runtime from the moving head of
`full-frontal`, while the editor AppImage that converts the assets carries the pinned importer. This
change touches neither the asset format nor the importer, so the two cannot disagree because of it.

## 5. Risks

- **The hot path.** `t3d_anim_update` runs every frame for every playing clip and calls `load_keyframe`.
  At tiny3d's `-Os`, `load_keyframe` is a separate function, not inlined. Its return value changes from
  a bool to a pointer it already holds, and the caller only tests it. Checked with
  `mips64-elf-objdump -d build/t3danim.o` in `~/.local/src/tiny3d`:
  - `t3d_anim_update` compiles to the same 174 instructions;
  - `load_keyframe` loses only its `li v0,1`.
- **The float comparison.** `timeStart == 0.0f` is exact here: `timeStart` is a plain copy of the old
  end time, which is exactly 0 until the channel's first keyframe with a nonzero time (section 3).
- **A channel that is never filled** (its keyframes never move its end time off 0). The loop reads to
  the end of the stream, as it does today. It now costs O(K) for K keyframes, instead of O(K × C).

## 6. How to test it

1. **Host replay (done):** the replay above reads the same keyframes for all 32 clips.
2. **Identical poses:** a debug build dumps every bone's position, rotation and scale right after attach
   and after the first update, for each RetroStar and Minion clip. Run it with the old and the new
   library; the dumps must match exactly.
3. **Timing:** put the `[PROF]` attach timing back in a scratch build and measure in ares, then on the
   SC64.
4. **By eye:** no popping in these cases:
   - the Player's blended state changes;
   - companions starting each clip;
   - Minion enemies;
   - the Dialog's talk clips.
5. **tiny3d's own examples:** `examples/08_animation` (blending) and `examples/96_animtest` (many
   instances) in ares. tiny3d's automated tests (`tests/perf`, `tests/rsp`) do not cover animation.

## 7. Order of work (as planned)

1. Commit the change on `full-frontal` locally, then build and install it into `~/.local/n64`.
2. Run section 6 **before pushing**. KC's CI and the Windows setup script take the branch head, so a
   push goes live for them at once.
3. Push tiny3d.
4. Optional: bump the pyrite64 submodule (and add a changelog line). This follows the usual order:
   engine first, wait for its release, then KC.
5. Kentucky Castle needs no code change; its next CI run rebuilds the toolchain.

**To undo it:** revert the commit on `full-frontal` and rebuild the local toolchain. CI rebuilds on the
next run.

## 8. Outcome (2026-11-27)

**Commits:**

| Repo | Branch | Commit | What |
|---|---|---|---|
| RetroZelda/tiny3d | `full-frontal` | 95bc862 | the change, on top of 63bafbb (a fast-forward) |
| RetroZelda/pyrite64 | `stream/full-frontal` | 65ee072 | the submodule bump plus a changelog line |
| local toolchain | `~/.local/n64` | — | `libt3d.a` rebuilt and installed; the headers did not change |

The pyrite64 commit was made with git plumbing, so the `feature/companions` working tree was not touched.
Its push started a Build & Release run.

**Tests (section 6):**

1. **Identical poses.** I ran a host harness: `t3danim.c` built for the PC, with stand-ins for the DMA
   and the model. It played every clip through these steps, with 64- and 512-byte stream buffers:
   - attach, then 2.5 loops of updates;
   - attach to a second skeleton (a blend's move), then 1.5 loops;
   - a `set_time` into the middle;
   - attach back, mid-clip.

   It wrote every bone value after every step. The old and new code gave byte-identical output (23.9 MB
   for KC's 35 RetroStar, Minion and Big Minion clips), with the same DMA counts.
2. **Sensitivity.** A deliberately broken variant, which stops priming halfway, gave the *same* output
   on KC's clips. The reason: every channel in them starts at time 0. The first update then reloads each
   channel's first two keyframes, which overwrites whatever priming set. **Priming only changes
   anything for a channel whose first keyframe comes after 0.** For that case I generated five synthetic
   clips in the importer's exact layout:
   - channels starting late;
   - one-keyframe channels;
   - keyframes at the same time;
   - a channel with no keyframes at all;
   - a three-channel clip.

   On these, the broken variant differs, and the old and new code are byte-identical. The empty channel
   reads to the end of the stream in both, as expected.
3. **Timing.** A scratch build of KC timed a full attach on every companion clip change, in ares:

   | Clip | Before | After |
   |---|---|---|
   | RetroStar (138–149 channels) | 9.5–12 ms | 1.4–2.2 ms |
   | 20-channel | 0.41 ms | 0.23–0.26 ms |

   Selecting a clip already attached costs about 0.2 ms, the rewind's blocking reload.
4. **By eye:** in ares, the Player and both companions walk, run and stop with normal poses, and the log
   has no errors. tiny3d's `examples/08_animation` idles, blends and attacks normally.

**What is left of the cost.** About 6.5 µs per keyframe read: the stream's DMA waits and the
quaternion unpacking, for 2 × C keyframes. Then about 0.2 ms for the rewind's blocking reload. Both grow
only with C. Two things could cut further, if ever needed:
- rewind without reloading when the start of the stream is still in the buffer;
- skip priming entirely for clips whose channels all start at 0. This needs a flag from the importer.

**The fork's branches.** The owner's model, the same as the pyrite64 fork:

- the working branch is the default branch (pyrite64 `stream/full-frontal`, tiny3d `full-frontal`);
- `main` is only the bridge that syncs with upstream, and it is never moved to the working branch.

In the tiny3d fork:

1. **The default branch is now `full-frontal`.** The owner switched it on 2026-11-27.
2. **`main` mirrors upstream.** At the owner's request it was force-pushed (with a lease) to 2f509c4.
   That is the tiny3d revision upstream pyrite64 `main` (59d48e6) pins, and upstream tiny3d's `main`
   head. It replaced a4638a9, the May fix on the old loader, which is still on `fix/jank-animation`.
   GitHub's compare shows the fork's `main` identical to upstream's.

Upstream has 7 commits after `full-frontal`'s base (6f5ac0b): importer fixes, examples and an rdpq change.
None of them touch `t3danim.c`. Merging them into `full-frontal` is a separate decision.
