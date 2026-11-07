# USB keyboard/mouse diagnostics (experimental)

Branch: `feature/usb-kbm-diagnostics`

This is a **USB discovery and diagnostics build**, not yet keyboard/mouse-to-DualSense support. It reads USB device/configuration descriptors but does not detach drivers, claim interfaces, or send USB commands. Existing Bluetooth pad behavior is unchanged.

## Testing on a PS5

1. Download the `anypad-usb-km-diagnostic-elf` artifact from the GitHub Actions workflow and load the ELF as usual.
2. Connect the keyboard and mouse directly to the PS5. Test a few harmless keys (W/A/S/D, Shift, Ctrl) and mouse movement.
3. Read `/data/anypad/anypad.log` via FTP. Find lines beginning `km_diag:`. The diagnostic scans `/dev` every two seconds, inspects descriptors for each `ugen*` device, and inventories matching endpoint nodes under `/dev/usb/`.
4. To enable optional raw report capture, create an **empty regular file** at `/data/anypad/km_capture`. For convenience, `/data/anypad/km_capture.txt` is also accepted. Do not use `km_capture.log` as the trigger: that is the **output file**.
5. With the trigger present, the diagnostic performs an **open-only probe** of HID interrupt-IN endpoint nodes discovered from the descriptors. It logs whether each endpoint can be opened, then immediately closes it. It does not read reports yet. Remove the trigger file after the probe lines appear.
6. Download and share:
   - `/data/anypad/anypad.log` — USB device IDs, interface classes, endpoints, kernel-driver names, capture status/errors.
   - `/data/anypad/km_capture.log` — raw reports from legacy `ukbd*`, `ums*`, or `uhid*` nodes, if available. It is expected to remain empty during the endpoint open-only test.

The raw capture path remains deliberately bounded (maximum 2,048 reports and 256 KiB). On the GameSir VXKB seen in the current test, the kernel binds the interfaces to `usb_hid0`–`usb_hid3`, not `ukbd*`, `ums*`, or `uhid*`. The current test therefore checks only whether descriptor-identified HID interrupt-IN endpoint nodes can be opened. A successful open does not yet prove that report reads are safe or that the device will continue working normally.

## Privacy and safety

Raw keyboard reports can reveal typed keys. Do not enter passwords, messages, or personal information while capture is enabled. The endpoint probe is opt-in, opens input endpoints read-only and immediately closes them; it does not read reports, detach USB drivers, or claim HID interfaces.

**Stop:** delete `/data/anypad/km_capture` (or `km_capture.txt`) to stop probing/capture; stop the payload from its normal dashboard or `/data/anypad/stop`.

## What to send back

Please share all `km_diag:` lines from `anypad.log`. For this test, `km_capture.log` is not expected to contain data.

