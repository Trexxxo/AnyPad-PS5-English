#ifndef ANYPAD_USB_KM_DIAG_H
#define ANYPAD_USB_KM_DIAG_H

/* Read-only USB node discovery. Report capture is explicitly opt-in. */
void usb_km_diag_init(void);
void usb_km_diag_poll(long now_ms);
void usb_km_diag_close(void);

#endif
