<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Sprite depth files (`SPD1`)

One file per character BAM: `<game>\pst_forever\assets\sprites\depth\<BAM>.DEP` (BAM resref upper case).
Written by `tools/sprite_depth/sprite_depth.py run`; the BAM list is `character_bams.txt`.
All integers little endian.

## Header (32 bytes)

| Off | Type | Field |
|---|---|---|
| 0x00 | char[4] | `SPD1` |
| 0x04 | u16 | version (1) |
| 0x06 | u16 | flags: bit 0 = samples are u16 (else u8) |
| 0x08 | u16 | frame count (= the BAM's frame entries) |
| 0x0A | u16 | cycle count (= the BAM's cycles) |
| 0x0C | u32 | offset of the frame table |
| 0x10 | u32 | offset of the cycle table |
| 0x14 | u32 | offset of the lookup table |
| 0x18 | u32 | CRC-32 (zlib) of the source BAM file bytes, as read (BAMC still compressed) |
| 0x1C | u32 | CRC-32 of the generator settings (0 = dome only, no model). A re-run skips a BAM when both CRCs match |

## Frame table (20 bytes per frame, in BAM frame-entry order)

| Off | Type | Field |
|---|---|---|
| 0x00 | u16 | width (= BAM frame width) |
| 0x02 | u16 | height |
| 0x04 | i16 | centre x (= BAM frame centre) |
| 0x06 | i16 | centre y |
| 0x08 | f32 | scale: pixels of height per sample unit |
| 0x0C | u32 | offset of the zlib stream (0 when empty) |
| 0x10 | u32 | size of the zlib stream (0 = the whole frame is 0) |

The zlib stream inflates to `width * height` samples (u16 or u8 by the flag), rows top-down,
the same pixel grid as the BAM frame. Height in screen pixels = sample * scale.

## Cycle table and lookup

Copies of the BAM's: per cycle `u16 count, u16 first lookup index`; the lookup table is
`u16` frame indices (length = max(first + count)). Frames are stored once per BAM frame entry, so
a frame shared by several cycles has one depth.

## Meaning of the samples

Height of the visible surface above the sprite's billboard plane (the flat upright plane the
engine draws the frame on), toward the camera, in screen pixels at 1x. Mirrored draws use the
same values (height does not change under a horizontal flip; the normal's x does).

- 0 where the palette index is 0 (transparent) or 1 (the shadow pixel), and on the outermost
  ring of the silhouette.
- Inside: a silhouette dome `h = r*sqrt(1-(1-d/r)^2)` (`d` = distance to the silhouette edge minus 1,
  index-1 spots enclosed by the figure count as inside for `d`; `r` = body radius of the BAM, the
  median over its frames of the largest `d`), plus 0.6 x the shape from a monocular depth model
  (Distill-Any-Depth-Large) fitted to that dome per frame and smoothed over the neighbouring frames
  of the cycle. Clipped to `[0, 1.5 r]`.
