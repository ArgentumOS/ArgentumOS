/*
 * fnx/drivers/usb/usb.c
 *
 * USB host controller dispatch. Probing is ordered - xhci first
 * (it covers both USB 2.0 and 3.0), then EHCI/UHCI when they exist.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/usb.h>

void usb_init(void)
{
	xhci_probe();
}
