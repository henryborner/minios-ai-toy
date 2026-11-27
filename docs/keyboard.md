# Keyboard mapping

- Left Shift `0x2A`, right Shift `0x36` keep independent pressed states;
  `shift_down` is their OR, so releasing one no longer clears the other.
- Num Lock `0x45` toggles `numlock_on`; it starts enabled.
- Keypad `0x47..0x53` maps to digits/operators when Num Lock is on, and to
  Home/arrows/PgUp/PgDn/End/Delete when off.
- Keypad `*` uses scan code `0x37`.
- Keypad Enter (`E0 1C`) emits newline.
- Extended right Ctrl is decoded (`E0 1D/9D`).

Verified in QEMU:

```text
sendkey shift_r-a  -> A
sendkey kp_789     -> 789
sendkey kp_123     -> 123
```

No keyboard LED update is implemented. Caps Lock is still not implemented.
