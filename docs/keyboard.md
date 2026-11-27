# Keyboard mapping

- Left/right Shift, Ctrl and Alt have independent pressed states and an OR
  state, so releasing one does not clear the other.
- Caps Lock toggles letter case with `Shift XOR CapsLock`.
- Num Lock `0x45` toggles `numlock_on`; it starts enabled.
- Scroll Lock `0x46` is tracked.
- Keypad `0x47..0x53` maps to digits/operators when Num Lock is on, and to
  Home/arrows/PgUp/PgDn/End/Insert/Delete when off.
- Keypad `*` uses scan code `0x37`; keypad `/` uses extended `E0 35`.
- Keypad Enter (`E0 1C`) emits newline.
- PrintScreen (`E0 2A E0 37`) emits `KEY_PRINT` (`F5`).
- Pause (`E1 1D 45 ... C5`) is consumed as a sequence and emits `KEY_PAUSE`
  (`F4`), without toggling Num Lock.
- Caps/Num/Scroll LEDs are queued and sent with KBC `0xED` from the main loop.
- F1–F12 scan codes are decoded.

Headless regression:

```text
python tools/keyboard_test.py
```

It starts hidden QEMU, enables the serial `keydebug` probe, verifies Shift,
Caps, NumLock/LEDs, ScrollLock/LEDs, keypad digits/operators/Enter, navigation
keys, F1/F12, PrintScreen, Pause, and that Pause does not change Num Lock.

## ASCII layouts

`keymap` selects `us`, `uk`, `dvorak` or `colemak`; `Ctrl+Alt+1..4` selects
the same layouts without depending on the current letter positions.

```text
keymap us
keymap uk
keymap dvorak
keymap colemak
```

UK is ASCII-only: the two non-ASCII UK symbols use ASCII fallbacks (`£`→`#`,
`¬`→`~`). Dvorak/Colemak remap letters; symbol positions follow US where no
ASCII glyph exists.

The headless test now switches US→Dvorak→Colemak→UK→US and verifies physical
`r→p`, `e→f`, `3→#` and the return to US.

No international Latin-1/CJK layout is implemented; the terminal font is
8-bit ASCII.
