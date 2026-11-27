# GUI damage composition

## Implemented

- Bounded queue of 16 rectangles (`c/gui_damage.h`)
- Damage rectangles clipped and merged when the union does not increase area
- Overflow collapses the queue into a union so no damaged pixel is lost
- Per-canvas `unpublished` damage, published on `present`
- Partial composition clipped to each damage rectangle
- Old whole-window/whole-canvas redraw path removed
- Production `gui_redraw` retained as the full-redraw fallback

## Tests

`c/tests/gui_damage_test.py` builds a temporary test kernel, boots private
QEMU processes for both video modes, and compares partial composition against
a full-redraw snapshot pixel by pixel.

```text
VBE PASS mode=VBE resolution=640x480 pitch=640 checks=45
VGA PASS mode=VGA resolution=320x200 checks=45
```

Normal image:

```text
kernel:  100,256 / 131,072 B
BSS:     0x34c4f2
smoke:   PASS
```

The earlier claim of 21,000 clipping/overflow samples is not present in this
test suite; its output is 45 named checks per mode. The stale `__divdi3` test
stub was removed after verifying the production object has no 64-bit division
reference and links without a compiler runtime.

## Paint incremental path

- Pencil strokes now call `Canvas.line()` directly; no full-canvas upload.
- Line/rect/circle previews keep a base bytearray, redraw the shape into `pix`,
  and publish only the union of old/new 16x16 tiles with `Canvas.blit()`.
- Static toolbar/palette and initial image still use the full `draw()` path;
  fill/load remain full-canvas operations.
- The old 60 KB `bytes(pix)` upload per pencil event is gone.

Normal image and VBE/VGA damage tests still pass. The next remaining hot spot
is full-canvas fill/load and the remaining Python-side shape synthesis.
