# USB keyboard/mouse diagnostics (experimental)

Branch: `feature/usb-kbm-diagnostics`

This is a **USB discovery and diagnostics build**, not yet keyboard/mouse-to-DualSense support. It reads USB device/configuration descriptors but does not detach drivers, claim interfaces, or send USB commands. Existing Bluetooth pad behavior is unchanged.

## Testing on a PS5

1. Download the `anypad-usb-km-diagnostic-elf` artifact from the GitHub Actions workflow and load the ELF as usual.
2. Connect the keyboard and mouse directly to the PS5. Test a few harmless keys (W/A/S/D, Shift, Ctrl) and mouse movement.
3. Read `/data/anypad/anypad.log` via FTP. Find lines beginning `km_diag:`. The diagnostic scans `/dev` every two seconds, inspects descriptors for each `ugen*` device, and inventories matching endpoint nodes under `/dev/usb/`.
4. To enable optional raw report capture, create an **empty regular file** at `/data/anypad/km_capture`. For convenience, `/data/anypad/km_capture.txt` is also accepted. Do not use `km_capture.log` as the trigger: that is the **output file**.
5. With the trigger present, the diagnostic performs the open-only probe and then attempts bounded reads from only the boot keyboard and boot mouse endpoints (identified by interface subclass/protocol). Use harmless test input only: tap W/A/S/D and move the mouse for a few seconds. Do not type passwords or personal messages. Remove the trigger file immediately after the test.
6. Download and share:
   - `/data/anypad/anypad.log` — USB device IDs, interface classes, endpoints, kernel-driver names, capture status/errors.
   - `/data/anypad/km_capture.log` — bounded raw reports, if the PS5 generic endpoint read succeeds. Lines identify the device, keyboard/mouse role, endpoint, path, length, and report bytes.

The raw capture path remains deliberately bounded (maximum 2,048 reports and 256 KiB). On the GameSir VXKB seen in the current test, the kernel binds the interfaces to `usb_hid0`–`usb_hid3`, not `ukbd*`, `ums*`, or `uhid*`. The diagnostic reads only interfaces with boot subclass 1 and protocol 2 (keyboard) or protocol 1 (mouse). It does not read the two additional vendor/consumer HID interfaces. The reads are opt-in and bounded to 2,048 reports / 256 KiB. Because generic endpoint reads may compete with the system HID driver's polling, verify that normal keyboard/mouse input still works during the short test. A failed read or missing report is useful diagnostic evidence; do not detach the system driver. If a nonblocking read returns `EAGAIN`, the diagnostic now records periodic "no data yet" counters in `anypad.log`. If it returns zero bytes, it logs the event but keeps the endpoint reader open instead of immediately closing it: upstream FreeBSD `ugen` can use a zero-length record while recovering from an interrupt-transfer error, and closing immediately may interrupt that recovery. This behavior still needs confirmation against Sony's PS5 kernel.

## Privacy and safety

Raw keyboard reports can reveal typed keys. Do not enter passwords, messages, or personal information while capture is enabled. The endpoint probe is opt-in. The subsequent report test opens the boot keyboard/mouse input endpoints read-only and reads reports; it does not detach USB drivers or claim HID interfaces. Raw keyboard reports can expose key activity, so use harmless test keys only and remove the marker immediately after testing.

**Stop:** delete `/data/anypad/km_capture` (or `km_capture.txt`) to close the endpoint descriptors and stop reads; stop the payload from its normal dashboard or `/data/anypad/stop`.

## What to send back

Please share all `km_diag:` lines from `anypad.log` and `km_capture.log`, if it contains reports. Also tell me whether the keyboard and mouse continued to work normally while the marker was present.

