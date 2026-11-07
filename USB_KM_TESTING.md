# USB keyboard/mouse diagnostics (experimental)

Branch: `feature/usb-kbm-diagnostics`

This is a **USB discovery and diagnostics build**, not yet keyboard/mouse-to-DualSense support. It reads USB device/configuration descriptors but does not detach drivers, claim interfaces, or send USB commands. Existing Bluetooth pad behavior is unchanged.

## Testing on a PS5

1. Download the `anypad-usb-km-diagnostic-elf` artifact from the GitHub Actions workflow and load the ELF as usual.
2. Connect the keyboard and mouse directly to the PS5. Test a few harmless keys (W/A/S/D, Shift, Ctrl) and mouse movement.
3. Read `/data/anypad/anypad.log` via FTP. Find lines beginning `km_diag:`. The diagnostic scans `/dev` every two seconds, inspects descriptors for each `ugen*` device, and inventories matching endpoint nodes under `/dev/usb/`. It does not open or read those endpoint nodes yet; the PS5 reports the interfaces as owned by `usb_hid0`–`usb_hid3`, so we are verifying the endpoint-node layout before attempting any input reads.
4. To enable optional raw report capture, create an **empty regular file** at `/data/anypad/km_capture`. For convenience, `/data/anypad/km_capture.txt` is also accepted. Do not use `km_capture.log` as the trigger: that is the **output file**.
5. With the trigger present, press a few harmless keys and move/click the mouse for 10–15 seconds. Remove the trigger file to stop capture.
6. Download and share:
   - `/data/anypad/anypad.log` — USB device IDs, interface classes, endpoints, kernel-driver names, capture status/errors.
   - `/data/anypad/km_capture.log` — raw reports, if the PS5 exposes readable `ukbd*`, `ums*`, or `uhid*` device nodes and capture succeeds.

The raw capture is deliberately bounded (maximum 2,048 reports and 256 KiB). On the GameSir VXKB seen in the current test, the kernel binds the interfaces to `usb_hid0`–`usb_hid3`, not `ukbd*`, `ums*`, or `uhid*`. In that case `km_capture.log` is expected to remain empty. The next diagnostic output to inspect is the `endpoint-node` inventory in `anypad.log`; endpoint reads are intentionally disabled until we confirm the device-node mapping and avoid interfering with the system HID driver.

## Privacy and safety

Raw keyboard reports can reveal typed keys. Do not enter passwords, messages, or personal information while capture is enabled. Capture is opt-in and read-only. The payload does not detach USB drivers or claim HID interfaces. Remove the trigger immediately after the short test.

**Stop:** delete `/data/anypad/km_capture` (or `km_capture.txt`) to stop raw reads; stop the payload from its normal dashboard or `/data/anypad/stop`.

## What to send back

Please share all `km_diag:` lines from `anypad.log`, plus `km_capture.log` if it was created and contains reports. Redact any report bytes you do not want to share.

