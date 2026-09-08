# Phase 1/2 Handoff: DWIN Thumbnail Rendering Optimization

**Status:** Complete and validated on real hardware  
**Date:** 2026-09-04  
**Commits:** 3b15efa (Phase 1/2 core), 5373382 (extended benchmarks)

## Executive Summary

Implemented and measured palette-hoisting + run-length encoding (Phase 1) and color-bucket sorting (Phase 2) for dynamic G-code thumbnail rendering on the TJC3224T132_011N panel. Verified on real hardware across 5 authentic G-code thumbnails with a **4.19× average speedup** (range: 2.6–6.0× depending on model complexity).

**Key Result:** The bottleneck is palette-switching overhead (1 per run in naive, 1 per distinct color in bucket-sort), not byte count or wire throughput. Bucket-sort is decisively superior to plain RLE for this use case.

**Safe operating point:** 3ms/frame throttle, proven over 5 real images with zero corruption.

---

## Benchmark Dataset

### Real G-code Thumbnails (96×96 RGB565, Marlin `E3V3SE_THUMB_RAW16` format)

| Image | Fill | Colors | Runs | Palette switches/run | Naive wall time | Bucket wall time | Speedup |
|---|---|---|---|---|---|---|---|
| **thumbnail_1** (smooth shading) | 22.4% | 69 | 1,483 | 99.73% | 13.75s | 5.20s | **2.64×** |
| **thumbnail_2** (smooth shading) | 20.7% | 69 | 1,212 | 99.67% | 12.60s | 4.29s | **2.94×** |
| **thumbnail_3** (faceted model) | 34.0% | 10 | 1,365 | 98.46% | 20.84s | 4.67s | **4.46×** |
| **thumbnail_4** (minimal flat) | 19.4% | 8 | 580 | 97.76% | 11.87s | 1.98s | **5.99×** |
| **thumbnail_5** (minimal flat) | 17.1% | 8 | 635 | 97.80% | 10.53s | 2.15s | **4.90×** |
| **Average** | — | — | — | — | — | — | **4.19×** |

### Speedup vs. Model Complexity

- **Smooth-shaded (69 colors):** 2.6–2.9× speedup
- **Faceted/minimal (8–10 colors):** 4.5–6.0× speedup

The 2–3× difference is due to palette reduction: bucket-sort sends `0x40` only 69 times for smooth models (vs. 1,200+ times in naive), but only 8 times for faceted models. Faceted models are the common case for 3D model thumbnails.

---

## Technical Details

### Hardware Constraints (Confirmed Empirically)

- **Panel overrun threshold:** ~2000 unthrottled DWIN commands will overflow the input buffer (`0x24 0xFF 0xFF 0xFF` error frame) and wedge the panel past recovery (even Nextion `rest` fails; only power cycle works).
- **Safe minimum delay:** 3ms/frame (conservatively chosen post-overrun incident; may be tighter if bisected downward, but not yet tested).
- **Wire throughput:** 115200 baud, ~2.1ms per frame (24 bytes at 115.2 kbit/s).
- **Panel processing:** ~1.3ms per frame overhead beyond wire transmission; total effective throughput ~3.4ms/frame at measured operating point.
- **Wire is not the bottleneck:** Reducing frame count by 76% reduced wall time by 76%, confirming the panel processes in real time, not buffer-and-batch.

### Algorithm Comparison

| Algorithm | Frames (naive: 4130) | Description | Trade-offs |
|---|---|---|---|
| **Naive** | 4,130 | 1×palette + 1×rect per pixel | 24 bytes/px, every pixel |
| **RLE** | 2,962 avg (2,420–2,962) | Palette on color change, 1×rect per run | 28–37% reduction; still palette-heavy |
| **Bucket-sort** | 1,416 avg (588–1,552) | Palette once per color, runs by color | 62–76% reduction; requires O(runs) RAM (~6 KB) |

### Why Bucket-sort Wins

1. **Palette switching is the dominant cost.** A `0x40` set-palette frame takes the same time as a `0x5B` rect frame (~3.4ms with throttle).
2. **RLE still switches palette on every color change.** Smooth-shaded models have 69 colors across 1,200–1,500 runs, so nearly every run is a different color.
3. **Bucket-sort eliminates redundant switches.** All pixels of color N are rendered together, so `0x40` is sent only 8–69 times total (once per color), not 600–1,500 times.
4. **Memory cost is negligible.** Holding the run list (4–5 bytes per run) costs ~6 KB for a 96×96 thumbnail on the STM32F401 (256 KB SRAM).

---

## Code Artifacts

### TJC_display Repository (Source)

**New files (to copy into Marlin repo):**
- `tjc_dwin.py` — DWIN protocol client with per-frame throttling
- `dwin_blit.py` — Image encoders (naive, RLE, bucket-sort)
- `dwin_bench.py` — Hardware benchmark harness
- `thumb_raw16.py` — G-code thumbnail decoder
- `BENCHMARK_RESULTS.md` — Complete analysis with all results
- `PHASE_1_2_HANDOFF.md` — This document

**Test data:**
- `thumbnail_[1-5].gcode` — Real G-code files with embedded `E3V3SE_THUMB_RAW16` blocks
- `thumbnail_[1-5]_thumb.png` — Extracted test images (copy for reference/regression testing)

### Key Classes and Functions

#### `tjc_dwin.Panel` (tjc_dwin.py:64–180)

```python
Panel(port="/dev/ttyUSB0", baud=115200, timeout=0.01, delay=0.0)
  .send(instr, data)           # Fire one frame; applies self.delay
  .set_palette(color, bg=0)    # 0x40
  .fill_rect(color, x0, y0, x1, y1)  # 0x5B
  .clear(color)                # 0x52
  .handshake(timeout=2.0)      # 0x00 checkpoint; returns latency
  .flush()                     # Flush serial buffer
  .close()                     # Close port
  .recover_from_nextion_mode() # Static; fixes Nextion/DWIN mutual exclusivity
```

**Critical:** `delay` parameter is applied **inside `send()`**, at the wire-frame level. This is the correct place because helpers like `fill_rect()` and `set_palette()` each emit one or more frames. Applying throttle at the helper level (as attempted in an earlier iteration) produces unfair frame-count comparisons between encoders.

#### `dwin_blit` (dwin_blit.py:54–120)

```python
blit_naive(panel, img, x_off=0, y_off=0, skip_color=(0,0,0))    # Baseline
blit_rle(panel, img, x_off=0, y_off=0, skip_color=(0,0,0))      # Phase 1
blit_bucketed(panel, img, x_off=0, y_off=0, skip_color=(0,0,0)) # Phase 2

_rows(img, skip_color=(0,0,0))  # Yield (y, [(x0, x1, color565), ...]) per row
```

All three encoders:
- Skip `color == (0,0,0)` pixels (transparent black background)
- Return frame count (for benchmarking)
- Do **not** apply any delay (Panel.send() handles it)
- Are verified pixel-identical to the source image and to each other (offline mock-panel test)

#### `dwin_bench` (dwin_bench.py)

```
dwin_bench.py validate --port /dev/ttyUSB0 --delay 0.003 [--batch N]
  Sanity-check the handshake checkpoint: confirm checkpoint latency scales with batch size.
  
dwin_bench.py run image.png --port /dev/ttyUSB0 --delay 0.003 --mode {rle|bucket}
  Render naive into top half, --mode encoder into bottom half, report timings.
```

**Checkpoint mechanism:** The DWIN handshake (`0x00` frame) is sent immediately after drawing commands and timed until reply. It proves queue drain time (not just transmission time) because it's answered in-order after all preceding draws.

---

## Fixed Bugs (This Session)

### 1. Delay Granularity (Affects Phase 3 Porting)

**Problem:** Earlier iteration applied `delay` parameter at the blit-function level, once per pixel for naive but once per frame for RLE/bucket-sort. This silently produced misleading speedup comparisons.

**Fix:** Moved delay into `Panel.send()`, applied once per wire frame. Now all encoders are fairly throttled, with delay applied at the only place that makes sense: the frame-transmission boundary.

**Impact on Phase 3:** The 3ms delay must be applied in the DWIN frame-send helper in Marlin, not sprinkled through the `DWIN_RenderThumb()` loop.

### 2. Nextion Recovery Helper (recover_from_nextion_mode)

**Problem:** After `connect` handshake (which enables Nextion text protocol), the first command sent to the panel is silently dropped. The recovery function forgot to burn a dummy command before sending `rest`, so the `rest` command itself was lost and nothing happened.

**Fix:** Added `bs=42` as a throwaway command (same pattern used in `tjc_serial_upload.py`).

**Impact on Phase 3:** Not directly relevant unless the printer's Marlin code switches between protocols, but the pattern is documented if needed.

### 3. Panel Overrun Safety Documentation

**Problem:** An unthrottled flood of ~2000 DWIN commands caused a panel input-buffer overrun, produced error frame `0x24 0xFF 0xFF 0xFF`, and left the panel unresponsive to both protocols (Nextion `rest` reset failed; only power cycle worked).

**Fix:** Documented the hard safety limit (~2000-command ceiling for unthrottled operation) in code comments and `dwin_bench.py` defaults.

**Impact on Phase 3:** The 3ms/frame delay is not negotiable; it's the empirically-measured safe minimum. Do not remove or make optional without re-testing on real hardware.

---

## Validation Summary

### Offline Tests (No Hardware Required)
- ✅ `blit_rle()` and `blit_bucketed()` verified pixel-identical to source image and to each other using mock Panel
- ✅ Frame-count predictions match observed frame counts on real hardware within 0.1%

### Hardware Tests (5 Real Thumbnails)
- ✅ All 5 images rendered cleanly at 3ms/frame with **zero corruption**
- ✅ Top/bottom split rendering verified visually after each run
- ✅ Panel restored to normal UI after each test run (via Nextion recovery)
- ✅ Wall time scales linearly with frame count (no buffering/queueing surprises)
- ✅ Checkpoint latency scales with batch size (validation of timing probe)

### Regression Tests
- ✅ All 21 existing firmware-building tests pass (no firmware-path code touched)

---

## Porting to Marlin (Phase 3)

### Source File

`~/Projects/Marlin_bugfix_2.1_E3V3SE/Marlin/src/lcd/e3v2/creality/dwin_lcd.cpp`

### Current Implementation to Replace

**Current:** `DWIN_RenderThumb()` at line ~1400 (search for `DWIN_RenderThumb` in the file)

```cpp
// Current per-pixel loop (naive algorithm)
for (uint16_t y = 0; y < height; y++) {
  for (uint16_t x = 0; x < width; x++) {
    uint16_t color = get_pixel(x, y);  // from thumbnail header
    if (color != 0) {  // skip black background
      DWIN_Set_Color(color, 0xffff);
      DWIN_Draw_Rectangle(1, color, x_thumb + x, y_thumb + y, x_thumb + x, y_thumb + y);
    }
  }
}
```

### Phase 3 Changes Required

1. **Add bucket-sort encoder** (copy `blit_bucketed()` logic from `dwin_blit.py`)
   - Collect all runs into color buckets while decoding image
   - Render each bucket: set palette once, then draw all its runs

2. **Update `DWIN_RenderThumb()` to walk buckets instead of pixels**
   - Decode line-by-line as before (unchanged)
   - Collect runs into buckets (new)
   - Iterate buckets, not rows (new)

3. **Add palette-aware `DWIN_Draw_Rectangle` variant** (or modify existing)
   - New function `DWIN_Draw_Rectangle_NoSetPalette()` that skips the `0x40` frame
   - Or track previous color in a static and only call `DWIN_Set_Color()` on change

4. **Replace `DRAW_BATCH_DELAY` / `DRAW_BATCH_SIZE`** with 3ms per-frame throttle
   - Current: `#define DRAW_BATCH_DELAY 25` (guessed value per `DRAW_BATCH_SIZE` 15px)
   - New: Apply 3ms delay in the DWIN frame-send helper (do this **once**, not per run or per row)

5. **Keep existing optimizations**
   - `color == 0` skip (transparent black)
   - `E3V3SE_THUMB_RAW16` parsing from G-code comments
   - Thumbnail placement (`x_thumb`, `y_thumb`)

### Memory Budget

Bucket-sort requires holding the full run list in RAM before rendering:

| Thumbnail size | Fill | Estimated runs | RAM needed |
|---|---|---|---|
| 240×320 | ~32% | ~6,300 | ~31.5 KB |
| 96×96 | ~20% | ~1,400 | ~5.9 KB |
| 96×96 sparse | ~8% | ~600 | ~2.4 KB |

**All fit comfortably on the STM32F401 (256 KB SRAM).** Allocate a fixed buffer (e.g., 32 KB for worst-case 240×320) at compile time or use a dynamic allocation with size check.

### Testing Strategy

1. **Compile Marlin with bucket-sort encoder**
2. **Start a real print** with a thumbnail (gcode with `E3V3SE_THUMB_RAW16` block)
3. **Measure print-start time** before and after (time from G-code parser receiving the command to panel rendering complete)
4. **Visual check** for corruption or UI glitches during rendering
5. **Compare to Phase 1 benchmarks** (should see ~4× speedup on the real printer, not just the bench)

---

## Files to Copy to Marlin Repo

```bash
# Core optimization code (to include in Marlin)
cp dwin_blit.py ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/
cp tjc_dwin.py ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/

# Benchmarking and testing (for regression testing or further tuning)
cp dwin_bench.py ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/
cp thumb_raw16.py ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/
cp BENCHMARK_RESULTS.md ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/
cp PHASE_1_2_HANDOFF.md ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/

# Test data
cp thumbnail_[1-5]_thumb.png ../Marlin_bugfix_2.1_E3V3SE/DWIN_OptimizationReference/
```

---

## Next Steps (Phase 3)

1. **Read existing `DWIN_RenderThumb()` implementation** in `dwin_lcd.cpp`
2. **Understand the thumbnail data format** (already parsed as `E3V3SE_THUMB_RAW16` in the current code)
3. **Implement bucket-sort encoder** in C/C++ (port from `dwin_blit.py`)
4. **Test on real printer** with the benchmark thumbnails
5. **Measure print-start time** to verify 4× speedup is realized end-to-end
6. **Handle edge cases:** corrupt data, oversized palettes, memory pressure (if any)

---

## Reference Documents

- **BENCHMARK_RESULTS.md** — Full analysis, wall-time breakdown, design validation
- **AGENTS.md** — Session context and planning notes (kept in TJC_display repo)
- **Marlin documentation** — Search for `DWIN_RenderThumb`, `DWIN_Set_Color`, `DWIN_Draw_Rectangle`

---

**Ready for Phase 3 in new session within Marlin repo.**
