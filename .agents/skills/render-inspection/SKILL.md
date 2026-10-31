---
name: render-inspection
description: Inspect NW3D renders and UI screenshots with numbers first and few pixels. Use before looking at any render, screenshot or image diff (rendering bugs, CPU/GPU parity, seams, holes, UI checks) to keep image tokens low.
---

# Render inspection: numbers first, pixels last

Image cost for Claude is about width x height / 750 tokens; a 1280x720 frame is ~1,200 tokens, a 32x32 crop is ~2.
Origin: `C:\NewEra3D\wiki\agent_board.md` (vision pillars, Claude's verdict 2026-10-21).

## Order of tools (stop at the first that answers)

1. Exact hash or metric, zero image tokens: `nw3d-gpu` parity numbers (hits, depth, normal, PSNR),
   viewer `hashLevel()`.
2. Text diff: `python scripts/inspect_render.py diff A.png B.png` -> PSNR, share off, FLIP if installed, error
   regions classified line / blob / speck with box and peak.
3. Text profiles: `inspect_render.py hist` on the iters view; `inspect_render.py holes` on the coverage view (cracks).
4. Diagnostic views instead of beauty renders: `nw3d-terrain --debug` / viewer `debug=` normals, iters, depth,
   prism, coverage, level (unlit, exact code values).
5. Only then pixels: `inspect_render.py diff A B --out sheet.png --top 6` -> contact sheet of 32x32 crops
   (A | B | diff x10, 4x): ~400 tokens for 6 regions.

## Screenshots of pages and apps

- Prefer text: `read_page`, `get_page_text`, `find`, `javascript_tool` returning numbers (computed styles, counts).
- When a picture is needed, use `scale` 0.4-0.6 or `zoom` on a region; never full-resolution full-page shots.
- One screenshot per state change to prove a result, not one per step.

## Do not

- Eyeball a beauty render to find a 1-pixel seam or a 5-degree normal tilt: use diff + normals view.
- Paste images into reports; quote the metric line and the sheet path.
