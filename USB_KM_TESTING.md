# USB keyboard/mouse diagnostics (experimental)

Branch: `feature/usb-kbm-diagnostics`

This is the **first hardware discovery build**, not yet keyboard/mouse-to-DualSense support. It deliberately does not detach USB interfaces or write to them. Existing Bluetooth pad behavior is unchanged.

## Testing on a PS5

1. Download the `anypad-usb-km-diagnostic-elf` artifact from the GitHub Actions workflow and load the ELF as usual.
2. Connect a standard USB keyboard and USB mouse to the PS5.
3. Read `/data/anypad/anypad.log` via FTP. Find lines beginning `km_diag:`.
4. The diagnostic scans `/dev` every two seconds and lists nodes beginning `ugen`, `ukbd`, `ums`, and `uhid`.
5. **Optional raw report probe:** create the empty file `/data/anypad/km_capture` and then press a few *non-sensitive test keys* and move the mouse for 10–15 seconds. Delete the flag immediately after testing. The payload will attempt nonblocking read-only opens on `ukbd`, `ums`, and `uhid` nodes and log up to 32 bytes per report.
6. Share only the `km_diag:` lines, redacting anything you consider sensitive. A report is not guaranteed: PS5 may expose keyboard/mouse through a different interface, or the OS may already own its HID endpoints.

**Privacy:** Raw keyboard reports can reveal what was typed. Do not enter passwords, messages, or personal information while `km_capture` is present. The log is local and follows AnyPad's existing 1 MiB rotation policy. Capturing input from a shared keyboard node can affect normal input, so enable it only for the short diagnostic test.

**Stop:** delete `/data/anypad/km_capture` to stop raw reads; stop the payload from its normal dashboard or `/data/anypad/stop`.

## Expected log examples

```text
km_diag: USB discovery enabled; raw HID reads disabled by default
km_diag: USB node /dev/ugen0.2 discovered
km_diag: 3 USB-related nodes; raw capture OFF
km_diag: open /dev/uhid0 FAILED errno=...
km_diag: report dev=uhid0 len=8 hex=...
```

These are **examples**, not confirmed console results.

## Next implementation milestone

Use the log to select a functioning input backend (native SceKeyboard/SceMouse APIs or USB HID). Once key and relative mouse events are verified, implement a mapping layer that feeds AnyPad's existing `pad_state` and virtual DualSense path. Right-stick translation will need calibration for per-game turn-speed caps and deadzones.
