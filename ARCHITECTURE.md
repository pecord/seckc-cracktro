# Architecture

A tour of how `SecKC // PSX` is put together, for anyone forking it to learn PS1
homebrew. It assumes a little C but no PlayStation background.

## The mental model: immediate mode, no scene graph

There is **no scene graph, no retained objects, and no z-buffer.** Every frame the
program walks a fixed list of effects, and each one writes GPU primitives (quads,
lines, sprites) straight into an **ordering table**. The GPU then draws that table
back-to-front. That's the whole engine.

Two flat mechanisms do all the work that an engine usually hides:

- **Depth** = the ordering-table slot a primitive is linked at. Lower slot =
  drawn later = on top. The chrome ball "weaves behind" the logo purely because
  its computed slot is sometimes deeper than the logo's. (See *Ordering table*.)
- **State** = a few module-level variables (star positions, the DVD's velocity,
  the CD state machine). Each effect owns its own; nothing is shared except the
  double buffer.

If you can hold "build a list of primitives, hand it to the GPU, flip buffers,
repeat" in your head, you understand the demo.

## The frame loop

`main.c` is the conductor. After `*_init()` brings up each subsystem, the loop is:

```c
while (1) {
    env = vu_envelope(audio_level(frame));   // music -> a 0..255 "loudness"
    audio_update(frame);                       // keep the CD-DA looping
    starfield_update(); dvd_update();          // advance animated state ONCE

    gpu_pass_begin();                          // clear this frame's table
    sphere_capture_reflection();               // last frame -> the ball's mirror
    scene_render(frame, env);                  // every effect emits primitives
    gpu_present();                             // wait for vblank, show it, flip
    frame++;
}
```

`scene_render()` is just the draw order, back to front:

```
vaporwave  ->  starfield  ->  DVD  ->  chrome ball  ->  skull  ->  scroller
```

Note the split between `*_update()` (advance state, called once) and
`*_render()` (emit primitives, no side effects). Most effects are a pure function
of `frame` and don't need an update step at all.

## gpu.c — the rendering core

### Double buffering

The PS1 has 1 MB of VRAM treated as a 1024×512 grid of 16-bit pixels. We place
two 320×240 framebuffers in it, stacked vertically. While the GPU *displays* one,
we *draw* into the other, then flip — so you never see a half-drawn frame.

`DB` bundles everything for one buffer: its display/draw rectangles, an ordering
table, and a scratch buffer for primitives. `db[0]` and `db[1]` are mirror images
(each displays the other's draw area), and `db_active` flips every `gpu_present()`.

### The ordering table (OT)

`db[active].ot` is an array of `OT_LEN` linked-list heads, one per depth slot.
`addPrim(ot + slot, prim)` links a primitive at that slot; `DrawOTag` walks the
table from the last slot to the first, drawing everything. Because it draws
**back-to-front**, deeper slots are painted over by shallower ones — painter's
algorithm, no z-buffer.

Backdrops hard-code deep slots (`OT_LEN - 1` … `OT_LEN - 5`). 3D objects compute
their slot from the GTE's averaged depth (`gte_avsz4`/`gte_stotz`) so they sort
against each other correctly.

Primitives are bump-allocated out of `db[active].p` via `db_nextpri`;
`gpu_pass_begin()` resets that pointer and clears the table.

### Helpers everyone uses

`add_line`, `add_fillstroke` (thicken a stroke into a filled quad — that's how the
vector font is "filled"), `project` (run a model vertex through the GTE),
`rgb5`/`isqrt32`. The GTE (Geometry Transformation Engine) is the PS1's fixed-
point vector coprocessor; `gte_*` macros load vertices and do the perspective
transform in hardware.

## backdrop.c — the synthwave world

All 2D, basically free: a gradient sky/ground split at the horizon, a banded
semicircle "sun" (stacked sprites with slits), a perspective wireframe canyon +
floor grid sharing one scroll phase (so it reads as driving forward), and a
parallax starfield. The starfield is the textbook update/render split — `update`
moves stars toward the camera, `render` projects and draws their streaks.

## logo.c — the SecKC identity

The skull is an ASCII-art image baked into a **TIM** (the PS1 texture format,
embedded in the executable via `incbin`). To give flat art depth, we stack
`TEX_SLICES` additively-blended copies of the textured quad along the Z axis —
black texels are transparent, so the green shape gains real thickness and reads as
a solid slab when it tumbles. The DVD bouncer is a single textured sprite,
recoloured on each wall hit (update/render split).

## text.c — the stroke font

`vecfont.h` is a tiny vector font: each glyph is a list of line segments on a 4×6
grid. `draw_text2d` renders a flat label; `scroller_render` places each glyph as a
billboard in 3D whose depth is a function of its **screen position** (a fixed
crest at the reading centre) so the word you're reading is always nearest and
brightest. Filled strokes come from `add_fillstroke`.

## sphere.c — the chrome ball

The flashiest trick, worth reading closely.

A UV sphere is tessellated once into model-space vertices. Each frame the ball is
spun and orbited, and every vertex is projected with the GTE. The reflection is a
**matcap**: a vertex's UV into an environment map comes from its **screen position
relative to the ball centre**, not its surface normal:

```c
u = ENV_W/2 + (screen_x - center_x) * (ENV_W/2) / radius;
v = ENV_H/2 + (screen_y - center_y) * (ENV_H/2) / radius;
```

Keying off screen position (rather than the spun normal) is the whole point: the
reflected image stays **anchored to the world** while the surface spins under it,
exactly like real chrome. Sample by the spun normal instead and the reflection
tumbles with the ball (which looked wrong — the sun went upside-down).

### What's in the env map

Two sources, chosen by `PERF_SPHERE_DYNAMIC`:

- **Baked** (`0`): a synthwave panorama built once at boot in `chrome_env_init`
  (neon sun, slits, magenta grid, stars). Cheap, but static.
- **Live** (`1`, default): each frame `sphere_capture_reflection` copies the
  *previously displayed frame* into the env map (downscaled, mirrored L-R like a
  convex mirror), so the ball mirrors the real scene — logo, grid, scroller.

### The feedback wrinkle (and a TODO)

The live capture is the previous frame, which **contains the ball** — so the ball
reflects itself reflecting itself: an infinite "Droste" spiral. We tame it by
resetting the env map to the baked panorama each frame and blending the live scene
over it at **50%**, which halves the self-reflection every bounce so it converges
in ~1 step.

The *exact* fix is a two-pass render: draw the scene once **without** the ball
into the env map, then again **with** it to the screen. The capture then never
contains the ball, so there's zero feedback and no latency — at the cost of ~2×
scene work (≈30fps). That's a worthwhile follow-up; the single-pass blend is what
ships today because it keeps 60fps and the ball still weaving behind the logo.

## audio.c — CD-DA + the VU

The soundtrack is a Red Book **CD-DA** track (track 2 on the disc). `audio_update`
is a small state machine that starts playback once rendering is up and re-seeks to
keep it looping, **muting across every seek** so you never hear the tail of the
track during the seek. The VU level that drives the skull's pulse is read from the
live CD play position (`CdlGetlocP`) indexed into a baked loudness envelope
(`vu_env.h`) — perfect sync with no costly per-frame audio reads.

Several lines here exist only to satisfy the browser's `pcsx_rearmed` core, which
mutes CD audio and drops SPU register writes addressed the way the SDK addresses
them; the comments call out each workaround. All are harmless on real hardware.

## config.h — the knobs

`PERF_*` toggles let you bisect cost or change behaviour from the compiler
(`-DPERF_SPHERE=0`, etc.):

| Flag | Effect |
|------|--------|
| `PERF_CDDA`          | CD-DA music playback |
| `PERF_VU_CAPTURE`    | live SPU metering (HW only) vs. the baked envelope |
| `PERF_BG`            | the full backdrop + additive blend |
| `PERF_SPHERE`        | the chrome ball |
| `PERF_SPHERE_DYNAMIC`| live scene reflection vs. the baked map |

## VRAM map

```
x:   0          320     448   512   640   768   896
   +-----------+-------+-----+-----+-----+-----+
y=0 | fb A      | skull | dvd |     | env | env |
    | 320x240   |       |     |     |work | base|
y=240| fb B     |       |(dvd@256)  |     |     |
    | 320x240   |       |     |     |     |     |
```

Framebuffers live at x0; everything past x320 is textures and the chrome ball's
two 128×128 env-map pages. Keeping these from overlapping is manual on the PS1 —
there's no allocator.

## Building & tooling

See the README. The Python tools under `tools/` regenerate the assets that aren't
checked in or are derived: `make_music.py` (the track), `make_loop.py` (trim to a
seamless CD loop), `make_vu.py` (the loudness envelope), `make_tex.py`/`make_dvd.py`
(textures). `render.sh` + `shot.lua` do a headless screenshot via PCSX-Redux for
quick visual checks.
