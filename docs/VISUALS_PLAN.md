# Lighting, depth and visual effects for PST:EE: investigation and plan

Status: phases 0, 1, 2, 4 and 5 done 2026-11-22 (`src/modules/lighting/`, `src/engine/gl_world.*`, ini `[lighting]`).
Sprite depth maps: all 1,154 character BAMs (`tools/sprite_depth/`, 338 MB in `<game>\pst_forever\assets\sprites\depth`),
used by phase 2 (they replace the phase-3 normal maps: normals come from the depth gradient).
Module name: `lighting` (ARCHITECTURE.md §3.3).
Rule from CLAUDE.md applies everywhere: loose on presentation, exact on anything a script can observe.
Every effect here is presentation only, behind an ini toggle, and all toggles off = vanilla.

## 1. Answers first

| Question | Answer |
|---|---|
| Can the GemRB height maps be used directly? | **Yes, as they are.** Same 123 WEDs, same area sizes, same art (EE PVRZ vs classic TIS differ only by DXT noise: mean 3.7 to 5.5/255 per area, worst single 64x64 tile 11.5/255, over all 123 areas). `height.bin`/`normal.bin`/`ao.bin`/`lights.txt` are in area pixels, so they line up with EE world coordinates. No regeneration needed. |
| Missing areas? | **None.** EE has no area that the GemRB set lacks, and vice versa (123/123, every one with `<WED>LM.bmp`). Small data gaps are listed in §3.3. |
| Sprite normal maps offline? | Yes, offline and cached. All 2,445 creature and spell BAMs are **byte-identical** between classic and EE (only GUI/inventory BAMs changed), so frames computed from classic or GemRB data key 1:1 to EE draws. Nobody has made sprite normals yet (GemRB uses a luminance bump). |
| Did the engine already separate emissive things? | **Partly, and usefully.** Every BAM v1 world sprite draw ends in `CVidCell::RenderTexture`, whose flags pick the blend: `flags & 0x208` nonzero = additive (glowing area animations, blended VVCs, projectiles). That is the emissive signal for effects. (Corrected in phase 4: `sub_6873C0`'s 0x40000000 is a *multiply* on BAM v2, not the glow.) Painted emitters in the backgrounds are not separated, but the GemRB lights pipeline already located 1,531 of them. Not tabled. |
| What do the LM maps encode? | One thing: a hand-painted **creature tint** (RGB per 16x12 cell), sampled bilinearly at an object's feet. No light positions, no direction, no height. EE also uses its luma for **stealth** (`CGameSprite::InDark` from `Hide` and `CheckModal`), so it is gameplay data: never alter it or `GetTintColor`. |
| Shadows: which technique? | Hybrid: screen-space occlusion in the light pass (height map + sprite buffer as occluders) for dynamic lights, plus a projected silhouette for the area key light. Engine contact shadows (BAM shadow pixels) stay. §5.3. |

## 2. What GemRB has, and what we take

Source: `D:\gemrb\LIGHTING_PLAN.md` (phases 0 to 4 done), `VFX_PLAN.md` (written, **no phase started**),
`NOTES.md` phases 4/5, outputs in `C:\Games\GemRB\upscaled\lighting\<WED>\` (1.1 GB, 123 areas).

| GemRB piece | Verdict for EE |
|---|---|
| `lights.txt` (1,531 lights: fire 830, magic 310, steady 129, pool 262; 111/123 with a `key` line) | **Use as is.** Parser is ~60 lines; port `AreaSceneData::ParseLights` (GPL, keep copyright). |
| `height.bin` GLH1 (u16, texel 2, unit 1/16 px, height above walkable floor) | **Use as is.** Upload as `R32F` (or `R16UI` and scale in GLSL). |
| `normal.bin` GLN1, `ao.bin` GLA1 | **Use as is.** |
| Coordinate model (`sin(elev)=0.75`, `T=1.285714`, `h = ground - y`, normals x right / y toward camera / z up) | **Port exactly.** It is what the maps were built with. |
| `LightFS` math: wrapped Lambert, falloff with the 0.72 y squash, height-map occlusion ray march, soft shoulder composite, highlight keep | **Port** (GLSL 330, `D:\gemrb\gemrb\plugins\GLVideo\GLShaders.h` 234-400). |
| `PostFS` bloom (soft-knee bright pass, 9-tap separable blur, screen-ish add) | **Port**, but feed it the emissive buffer, not plain scene brightness. |
| `ScenePolish::DrawShadows` projected silhouettes | **Port the projection** (`project(dirX, dirY, k, a)` with key direction). |
| GL plumbing (`GLVideoDriver`, light buffer, sprite depth texture) | **Redo.** GemRB owns its renderer; EE does not let us. See §4. |
| Offline scripts `D:\gemrb\tools\lighting\*.py` (+ `tools/upscale` readers, venv with torch cu128) | **Reuse for any re-run** (emissive masks, single-area fixes). No PVRZ reader there (not needed: art is identical). |
| `VFX_PLAN.md` design (one gather, per-BAM offline scan `vfxscan` to colour/intensity/radius/kind/shape, flicker, envelopes, line lights, 48-light budget) | **Reuse the design and the scan.** Its game facts are about classic PST + GemRB's own PRO/VVC files and are **wrong for EE** (EE ships 54 PRO, 9 VVC and ports the original hardcoded projectiles as `CProjectilePST*` classes). |
| Upscaled sprites `upscaled/sprites/bam/<BAM>/<frame>.png` (2x, 1,871 BAMs) | For the `hires` module, not this one; normals at 1x sample with the same UVs. |

## 3. EE data facts (measured 2026-11-22)

### 3.1 Areas
- 123 WEDs in `chitin.key`, none in `override` (PST-UB-Reloaded overrides 15 AREs only, no art). Area size from
  WED overlay 0 equals the GemRB `lights.txt` `size` line for **all 123**.
- Art: EE TIS v2 (12-byte tile refs into `A<NNNN><page>.PVRZ`, DXT1) vs classic paletted TIS: tile counts equal for all
  123, colours differ only by compression (numbers in §1). Script: scratchpad `cmp_art.py` (DXT1 decoder, reusable).
- `HT` maps: identical in all 123. EE's `CGameArea::GetHeightOffset` uses them like GemRB (sprite y nudge), not as lighting height.

### 3.2 Differences that touch the generated data
| File | Areas | Effect |
|---|---|---|
| `SR` search map | AR0202, AR0700, AR0901 changed in EE | calibration floor mask was classic; small local height error where cells changed. Check the review images; re-run depth for these 3 only if it shows. |
| `LM` | AR1501 changed | pool lights / boost used the classic LM; re-run the **lights stage** for AR1501 (minutes, no GPU model). |
| WED wall polygons | AR0104, AR0402, AR1900 | walls feed calibration and `wall_mask`; compare review images. |
| WED overlays | AR0402, AR0609, AR0612 | check what changed (overlay tilesets, not overlay 0 art). |
| WED door records only | 44 areas | no effect on heights. |
| UB-Reloaded AREs | 15 (incl. AR0508, AR0700, AR1900) | the light dedup against glowing area animations used classic AREs; recheck those 15. |

### 3.3 Known gaps in the GemRB outputs (from its progress notes, still true)
- AR1601: all 9 tiles unanchored, height about 0 everywhere. AR3008/3010/3011/3016: too little floor to fit (nan residual).
  8 more areas have 1 or 2 unanchored edge tiles (blend covers them).
- 14 areas with 0 baked lights (AR0106 AR0206 AR0209 AR0304 AR0401 AR0405 AR0609 AR1202 AR1404 AR3006 AR3007 AR3010 AR3012 AR3014);
  12 without a key line (AR0110 AR0202 AR0203 AR0511 AR0604 AR0605 AR0901 AR0903 AR1400 AR1500 AR1700 AR3011). AR0508 cave-mouth glow missed.
- Heights are clamped to [0, 1024]: anything **below** the walkable floor (chasms, pits, the void) is stored as 0. This is
  the one real reason to re-export data later (§5.4). Saturation at 1024 is minor (worst AR0708 3.5% of pixels).
- Maps describe overlay 0 in its default state: no animated overlays (water/lava), and open/closed door tile swaps use
  the height of the default art.

### 3.4 Effects
- EE: 2,680 BAM, 54 PRO, 9 VVC, 168 SPL. Spell visuals are mostly engine classes: `CProjectilePSTGameAnimatingObject`
  (the ported hardcoded PST projectiles, also `RenderSmall`, `MotionBlur`), `CProjectileBAM`, `CProjectileSkyStrike(BAM)`,
  `CProjectileCastingGlow`, `CProjectileSpellHit`, `CProjectileAreaFade`, `CVEFVidCell`, `CGameTemporal`, `CParticleBam`,
  area animations via `CGameStatic::RenderBam`. All of these take the LM tint from `CGameArea::GetTintColor` and draw
  through `CVidCell::Render3d`.

## 4. How EE draws (the integration surface)

| Fact | Evidence (Linux name, Windows EA from ported names: confirm before hooking) |
|---|---|
| Renderer is a small immediate-mode GL layer `Draw*_GL`, GLES2-style shaders loaded **as game resources** (type 0x405 GLSL) by `DrawInit_GL` -> `DrawCreateProgram_GL(slot, vs, fs)`: 0 `vpDraw/fpDraw`, 1 `fpTone`, 2 `vpBlit/fpCatRom`, 3/4 YUV, 5 `fpSprite`, 6 `fpFONT`, 7 `fpSELECT`, 8 `fpSEAM` | `DrawInit_GL` win 1404B6080, `DrawCreateProgram_GL` 1404B4A60 |
| Area tiles are drawn with `fpSEAM` (bilinear across 64 px tile seams, colour tone, gamma/brightness). Sprites with `fpDraw` (texel x vertex colour) or `fpSprite` (sprite outline blur option). | `FPSEAM.GLSL`, `FPDRAW.GLSL`, `FPSPRITE.GLSL` |
| All world sprites go through one routine: `DrawEnable`, `DrawBlendFunc`, `DrawColor(alpha<<24 | tint)`, flag **0x40000000 switches to the glow blend**, `DrawColorTone`, then quads (BAM v1 textures or BAM v2 PVRZ pages). The LM tint therefore arrives as the **vertex colour**, separable in a shader. | `sub_6873C0` win 1404AE640 (callers: the three `CVidCell::Render3d`) |
| Zoom renders the world into an engine FBO (`DrawBeginScaled_GL` binds it, `DrawEndScaled_GL` blits it back); without zoom it draws to the back buffer. Draws are batched; state lives in a cached bitfield and is flushed by `sub_63A370`. | `DrawBeginScaled_GL` 1404B4340, `DrawEndScaled_GL` 1404B4E20 |
| No lighting system: `CInfinity::UpdateLighting`/`GetGlobalLighting` are the day/night global tint. Context: default (compatibility) profile, 16-bit depth, no stencil. | `CVidMode::SetDisplayMode` |
| Our existing hook already wraps the world pass (`CGameArea::Render` call site, pre and post callbacks, SEH guarded). | `src/engine/render.cpp` |

Consequences:
1. We can add our own GL programs and textures on the game thread (function pointers via `wglGetProcAddress`), but must
   flush the engine's batch before touching state and restore whatever its cached bitfield believes is bound.
2. A world-only target exists or is cheap to force: redirect the world pass to our FBO (as `DrawBeginScaled` does).
3. Replacing the sprite/tile **program handles** after `DrawInit_GL` (not shipping override GLSL files) gives MRT output
   (albedo, sprite info, emissive) while keeping the engine's uniforms; restoring the original handles = vanilla.

## 5. Design

### 5.0 Frame structure (one module, five shader programs)

```
world pass (engine draws as usual, into our FBO with 3 targets)
  T0 colour   : what the engine draws (sprites WITHOUT the LM tint when lm_ambient is on, see 5.6)
  T1 sprite   : per-pixel ground line (feet y), sprite normal (oct-encoded), LM tint; 0 on background
  T2 emissive : RGB of glow-blend draws (+ background emissive mask), RGBA16F
light pass   (half res)  ambient clear -> + each light quad: height occlusion, normals (area map or sprite atlas)
shadow pass  (sprite key-light silhouettes, multiplied into the light buffer)
composite    scene x light (soft shoulder) + emissive keep, atmosphere, bloom from T2
then the engine continues: fog of war, overhead text, UI
```

Programs: `light` (accumulate), `shadow`, `composite` (light x scene + atmosphere), `bloom` (bright pass + blur, on T2),
and the two MRT variants of the engine's own sprite and tile programs. That is the whole architecture; each feature is
a toggle inside one of them, not its own pass.

### 5.1 Area dynamic lighting and height occlusion (items 1, 8)
- Lights per frame: baked `lights.txt` (flicker by kind on the presentation clock, GemRB constants), effect lights (5.5),
  optional party light. Cull to the view, budget 64.
- Height map replaces the flat floor: `ground = m.y + heightAt(m)`; sprite pixels use T1's feet line. Occlusion = the
  GemRB ray march (16 steps, bias 2, thickness 48), plus T1 sprites as occluders (5.3).
- Area normals from `normal.bin`; AO from `ao.bin` on sprites only (backgrounds already have baked AO).
- In front / behind: EE already resolves sprite vs wall order with wall polygons; the height map is for light only.
  We never change draw order.

### 5.2 Sprite normal maps (item 2)
Offline tool `tools/sprite_normals.py` (CPU, numpy/scipy, no model):
1. Decode every frame of the 2,445 shared BAMs (classic or EE; identical). Index 0 transparent; index 1 is the
   semi-transparent **shadow** pixel: excluded from the surface, kept as is.
2. Height proxy: signed distance from the silhouette, `h = r * sqrt(1 - (1 - d/r)^2)` capped at a body radius `r` from the
   frame's opaque width (a soft dome), plus a low-weight high-pass of luma (detail, weight about 0.15) so painted folds
   read without turning noisy.
3. Normal from the gradient, `z` dominant (normal strength low by default); at the silhouette the normal tilts outward
   (rim light) but never past 60 degrees. Mirrored draws flip `n.x` in the shader.
4. Store per BAM one file `assets/sprites/nrm/<BAM>.nrm` (header, per-frame size and offset, RG8 oct-encoded normals,
   zlib). One file per BAM instead of 150k PNGs. Measure size in phase 3.
5. Runtime: the sprite hook knows the `CVidCell` (resref, sequence, frame: offsets to find in phase 0) and binds the
   frame's normal texture on unit 1; the MRT sprite program writes it to T1 with the same UV.
- Sprite depth: not generated. A billboard (pixel height = feet y - pixel y) is what the light model uses for
  sprites, and it is correct for upright figures; the dome only shapes normals.
- Art style guard: lights modulate around the engine's own result (ratio vs a flat floor normal, as GemRB), and
  `sprite_normal_strength` defaults to 0.5.

### 5.3 Shadows (item 3)
- **Dynamic lights (multi-light, coloured, occluded):** in the light pass, each light's ray march also tests T1:
  a sample whose screen point is a sprite pixel with ground near the sample's ground blocks that light. Result:
  every light casts its own shadow of every visible creature, length and softness from the light's height and
  distance, attached at the feet by construction (rays start on the floor). Walls and objects occlude through the height map.
  Limit: occluders must be on screen (fine at view edges with a margin).
- **Key light** (`key` line, 111 areas): projected silhouette of the current frame along the key direction, ported from
  `ScenePolish::DrawShadows`, blurred, alpha by key strength, clipped where the height map rises (no shadow pasted over walls).
- **Engine shadows:** the BAM shadow pixels stay. `shadows_replace_contact=0` by default.

### 5.4 Atmospheric perspective (item 4)
Honest limit first: the camera is orthographic, so on the floor "distance" is just screen height, and haze by screen y
would be a tilt-shift gradient, not depth. What the data can support:
- **Vertical depth cue:** surfaces with large height (tall walls, spires, the Sigil skyline) and the 1024-saturated
  far backdrops get a faint lift toward the area ambient colour and a small saturation and contrast drop
  (defaults: haze 0.06, desaturation 0.08, contrast 0.05, ramp over h 200..1024).
- **Below-floor depth** (chasms, the void around the Fortress, Curst's drop-offs) is where depth haze matters most and the
  current data has **nothing** (clamped to 0). Technical reason to re-export: run the GemRB depth stage again with the
  clamp widened to [-1024, 1024] and write a signed `depth.bin` (GLD1). Same model and calibration, about an hour of GPU.
  Do this only after the v1 cue is judged.
- Colour of the haze: key-line ambient, else LM area tint, so each area keeps its own mood.

### 5.5 Emissive and bloom (item 5)
- Effect emissive: the MRT sprite program writes `rgb * alpha * gain` to T2 when the glow-blend flag (0x40000000) is set.
- Background emissive: a mask per area from the lights stage's emitter cores (6.1 A in LIGHTING_PLAN), written as
  `emissive.bin` (GLE1, 8-bit, texel 2) by a **lights-stage re-run** (no depth model). Painted lamps then glow and bloom.
- Bloom only from T2 (not from bright floors), soft knee, defaults low (`bloom_strength=0.35`).
- Glow and light are separate: an emissive pixel only blooms; a light only comes from `lights.txt` or 5.6.
  Per-BAM table can give glow without light (`INTENSITY 0`) and vice versa.

### 5.6 Spell effect lighting (item 6)
- **Identify:** at runtime, any world draw with the glow flag, plus a table of non-blended BAMs that should still emit
  (hand list, empty at start). Dark kinds (smoke, `DARK`) never emit. Colourful but normally blended sprites (blood, gore)
  are excluded by the flag itself.
- **Analyse offline:** port `vfxscan` (VFX_PLAN 7.2) over every BAM those classes reference: per **frame**
  (not just the peak) luminance-weighted colour, bright centroid, bright area, kind, shape. Writes
  `assets/vfx/vfxauto.tsv`; hand overrides in `vfxlight.tsv`.
- **Associate and follow:** the sprite hook sees the cell, its frame and its screen rect, so the light sits on the drawn
  frame's centroid every frame; it follows projectiles, sticky VVCs and casting glows with no per-class code. Ground point:
  the owning object's feet when the draw happens inside a hooked object `Render` (sprite, temporal, projectile),
  else the frame's bottom.
- **One gather:** all effect draws of a frame go into one list; lights, bloom, debug overlay read it (GemRB rule).
- **No duplicates:** EE has no lights of its own. Glowing area animations become lights here, and `lights.txt` already
  excludes painted emitters within 120 px of a glowing animation, so each source gets exactly one light.
- Shaders: additive emissive write (5.5), colour from the frame table, smoothed intensity (tau 50 ms), flicker by kind;
  line lights for bolts (VFX_PLAN 7.5). Distortion/heat haze: later, optional.

### 5.7 LM maps (item 7)
- Keep EE's tint exactly where gameplay reads it. In the shader, treat the vertex-colour tint as the sprite's
  **ambient term** when lighting is on: `sprite = albedo * (lmTint * ambientScale + sum(lights * N.L) + emissive)`.
  So a creature in a painted dark nook stays dark, and a torch next to it still lights its lit side. (GemRB fought double
  darkening by scaling AO by LM luma; this avoids it at the source.)
- Per-pixel tint: sample LM at `(pixel x, feet y)` per sprite pixel instead of one colour per sprite, so a body crossing
  a tint edge blends smoothly (horizontal only; LM is a floor map).
- As light hints: already used (warm spots -> `pool` lights, large regions -> ambient tint). No positions or direction to
  extract: the key direction comes from the normals vs luma fit, not from LM.

## 6. Phases (each ends with tests at the cheapest layer, CLAUDE.md rules)

Done (phase 0/1 results, 2026-11-22):
- Composite point: the first of four `call` sites in `CGameArea::Render` (float text 14019EAC7, transitions 14019EB23,
  fog 14019EB4F, PostRender 14019EB95), armed once per frame from the world pre-draw hook. Fog, overhead text and
  markers stay unlit. No FBO redirection was needed: the engine's batch is flushed (`1404B51E0`), the current
  framebuffer's viewport is copied into our texture and drawn back lit. Every GL state we touch is saved/restored.
- Framebuffer -> area: the engine's own `uST` (slot 1, `14601AD88`) plus `off = view - rect.left`. On this machine
  (2560x1440) the world draws in the back-buffer branch, uST = (2/1190, -2/669, -1, 1): zoomed 2.15x.
- Round trip (debug mode 2) = **0 of 3,686,400 px differ** in AR1400, AR0400, AR0700. The engine leaves a GL error
  0x500 pending before our pass (its own; ours is 0).
- GPU light buffer = CPU mirror (`DebugFalloffAt`): 0 of 2,592 (AR1400) / 137 (AR0700) sampled pixels off by > 0.02,
  max error 0.0002. Occlusion blocks most of a wall-mounted light's own wall (AR1400: 553 of 2,592 lit): the GemRB
  model on this data, not a port error.
- A/B shots: only saves in AR1400, AR0400, AR0700 exist; script area travel (`C:Eval` LeaveAreaLUA[Panic],
  ActionOverride, MoveGlobalsTo) does not move the party, so AR0202/AR0105 need a save there.
- AO is loaded and uploaded but applies to sprites only (§5.1): it is wired in phase 2 with the sprite buffer.
  Until phase 2, sprites are lit like the background under them.

Done (phase 4/5 results, 2026-11-22):
- Glow signal: `CVidCell::RenderTexture` (1404AEA60, every BAM v1 world draw) blend from `flags & 0x208`: 0 normal,
  8 (1-DST,ONE), 0x200 (DST,ONE), 0x208 (SRC,ONE); bit 29 = multiply. Engine blend enum -> GL from lin unk_C08260.
  BAM v2 `sub_6873C0` has no additive mode (its 0x40000000 is DST_COLOR, 1-SRC_ALPHA).
- Cell identity: `Render3d`/`FXRender3d` prologue hooks; effects draw `CInfinity::FXRender(cell)` -> `FXRender3d`
  into the FX surface, then `FXBltToBack` -> `RenderTexture` without a cell, so the last FX cell is used.
- `tools/vfxscan.py`: 1,307 BAMs, 21,996 frames -> `assets/vfx/vfxauto.tsv` (1.1 MB, 22 s). Lookup by
  (resref, sequence +0x11A, frame +0x118): every glow draw of the Magic Missile cast was found.
- In game (Hive, Dak'kon casts SPWI107): 6 glow draws, 6 table hits, up to 3 effect lights, nearest 57-80 px from
  the caster; bloom max 0.55; overhead text stays unlit and above. No light comes from glow draws not in the table.
- Most PST spell visuals are normal-alpha, not additive (casting effects A001..A010 named in
  CGameSprite::ApplyCastingEffect, projectiles like S031AXST in CProjectile::DecodeProjectile): emitters are
  additive draws + vfxemit.txt (819 BAMs). Per-pixel emissive capture (re-draw through RenderTexture into our
  buffer) for own-cell and additive draws; FX-surface normal blits are composites (creature + attached effect),
  lights only. The first rect mask made the "white box" (un-darkened, bloomed frame rects): replaced.
- Hand lights (vfxlight.tsv, Ignus) and buff colour lights (ops 8/9/52/61, 353 TintScreen = the buffs' fade).
- Open: offline per-frame emissive maps so FX-surface effects get a pixel mask too (needs the effect frame's
  offset inside the FX composite), and the effect look (soft glow / additive boost) on top of the mask.
- Atmosphere v1: haze/desat/contrast by height (200..1024). The void between floors is stored as a saturated 1024
  backdrop, so the effect is gated to visible surfaces (luma 0.02..0.15 fade-in): voids stay exactly black.
  Share of hazed view: AR1400 8.4 % (mean t 0.30), AR0700 13.6 % (t 0.014), AR0400 0.3 %. Subtle by design; the
  signed depth re-export (§5.4) is still undecided.

Done (phase 2 + effect follow-ups, 2026-11-22):
- FX surface: every object draws `CInfinity::FXRender` (one cell) then one `FXBltToBack`; measured blits = cells
  (8,044 / 8,044). Cell draw point from the `FXRender3d` x/y, frame size/centre from cell +0x128 (often unloaded
  for still frames: the .DEP frame table has the same numbers), blit flag 0x10 mirrors (`FxCellRect`). Casting
  glow anchor = the caster's feet to the pixel (lighting_fx asserts it).
- So the "per-frame offline emissive masks" are not needed: a blit of an emitter is exactly the effect's pixels,
  captured per pixel; the mask is computed from them (`EmissiveWeight`: brightness x saturation), hand overrides in
  `data/vfxglow.tsv`. Normal-alpha effects are softened to the screen blend (`fx_soften`); look on top: boost,
  colour fringe, wider bloom (`fx_boost`, `fx_fringe`, `bloom_passes`).
- Sprite buffer: creature blits (one non-emitting cell with a .DEP) record feet anchor + mirror and re-draw into
  a coverage buffer (alpha; the engine writes with alpha masked, so the mask is forced on); at the composite one
  quad per creature writes z above feet, depth, LM level (`CGameArea::GetTintColor` at the feet) and the normal
  from the depth gradient (RGBA16F, `PackSpriteG`). Hive: 20 creatures, 103,263 buffer px, 99.7 % covered.
- Light pass: sprite pixels are lit as billboards with depth (P = x, feet + depth, z), Lambert on the depth
  normal; creatures occlude lights in the same ray march (a sample whose screen point is a creature pixel with
  feet on the sample's ground line). Composite: sprite = ambient + lights above ambient / LM (`SpriteLight`).
- Key shadows: GemRB `project` (k 0.85, y x 0.72, sideways bias), |sy| >= 0.22, blend MAX, no shadow where the
  height map rises; floor pixels only. The engine's contact blob (palette index 1) stays and is not in the
  depth maps (it is lit as floor).

0. **Probes and plumbing.** Confirm on both sides and add to the manifest: `sub_6873C0`, `DrawInit_GL`, `DrawCreateProgram_GL`,
   `DrawBeginScaled_GL`/`DrawEndScaled_GL`, the batch flush, the program table, `CVidCell` resref/sequence/frame offsets.
   Find where fog of war is drawn relative to objects (composite must run before it). Probe `PSTF_LightDebug(mode)`.
   Test: static manifest; in game, our FBO round trip with lighting off = pixel-identical shot.
1. **Baked lights + height + occlusion + area normals/AO.** Copy `C:\Games\GemRB\upscaled\lighting` to
   `<game>\pst_forever\assets\lighting\`. Unit: GLH1/GLN1/GLA1/lights parsers, CPU mirrors of `heightAt`/occlusion/falloff
   with values from GemRB's tests. In game: AR0202 and AR0105 A/B shots, `lighting:` log line per area.
2. **Sprite MRT + LM as ambient + key-light shadows + screen-space sprite occlusion.**
3. **Sprite normals** (offline tool, `.nrm` loader, unit tests on synthetic silhouettes: flat interior `n=(0,0,1)`,
   edges outward, mirrored flip).
4. **Emissive + bloom + effect lights** (vfxscan port, gather, tables). In game: SPWI107 path lit, casting glow lights the caster.
5. **Atmosphere v1**, then decide on the signed depth re-export.
6. Data fixes from §3.2/3.3 (AR1501 lights, the 3 SR areas, 15 UB AREs), emissive masks.

Ini `[lighting]` (all 0 = no hooks): `enabled baked_lights light_gain height_map occlusion occlusion_steps area_normals
sprite_ao key_light shadows sprite_normals sprite_normal_strength lm_ambient emissive bloom bloom_strength fx_lights
fx_light_gain atmosphere atmosphere_strength`.

## 7. Risks
- Engine GL state cache: our calls between its batches must leave its cached state true. Highest risk; phase 0 exists for it.
- Users with `Shaders=0` in Baldur.lua: the module stays off.
- `hires` (upscaled art) will change texture sizes; all our lookups are in area pixels or UVs, so they survive it.
- Performance at 4K: light pass at half res, 16 occlusion steps, budget 64 lights; measure with the smooth module's fps probe.
