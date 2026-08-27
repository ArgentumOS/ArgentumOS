/*
 * fnx/include/fnx/usb.h
 *
 * USB host controller dispatch. xhci.c is the first HCD; EHCI/UHCI
 * will probe here too when implemented.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_USB_H
#define _FNX_USB_H

int xhci_probe(void);
void usb_init(void);

#endif /* _FNX_USB_H */
