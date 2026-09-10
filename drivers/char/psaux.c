/*
 * fnx/drivers/char/psaux.c
 *
 * The PS/2 auxiliary (mouse) port of the 8042 controller. This driver
 * owns the controller handshake and IRQ12, and it is the PS/2 *producer*
 * for the native mouse device: every aux byte is handed to
 * mousedev_ps2_byte(), which frames 3-byte PS/2 packets and decodes
 * them into normalized records. There is no raw byte device any more -
 * consumers (the X server) read normalized events from
 * /System/Devices/mouse, never a PS/2 stream.
 *
 * Copyright 2024, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/devices.h>
#include <fnx/fs.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/ps2.h>
#include <fnx/psaux.h>
#include <fnx/mousedev.h>
#include <fnx/pic.h>
#include <fnx/irq.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_PSAUX

static struct interrupt irq_config_psaux = { 0, "psaux", &irq_psaux, NULL };

extern volatile unsigned char ack;
static unsigned char status[3] = { 0, 0, 0};
static unsigned char is_ps2 = 0;
static char id = -1;

static int psaux_command_write(const unsigned char byte)
{
	ps2_write(PS2_COMMAND, PS2_CMD_CH2_PREFIX);
	ps2_write(PS2_DATA, byte);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on %x command!\n", __FUNCTION__, byte);
		return 1;
	}
	return 0;
}

static void psaux_identify(void)
{
	/* disable */
	psaux_command_write(PS2_AUX_DISABLE);

	/* status information */
	psaux_command_write(PS2_DEV_GETINFO);
	status[0] = ps2_read(PS2_DATA);	/* status */
	status[1] = ps2_read(PS2_DATA);	/* resolution */
	status[2] = ps2_read(PS2_DATA);	/* sample rate */

	/* identify: plain IDENTIFY only. Do NOT run the IntelliMouse
	 * sample-rate magic (200/100/80) here: it switches a wheel mouse
	 * into IMPS/2 4-byte packet mode, and the decoder in mousedev.c
	 * frames 3-byte packets. Keeping the device in standard 3-byte
	 * mode makes the PS/2 stream unambiguous (a wheel mouse on the
	 * 8042 port simply reports no wheel). */
	psaux_command_write(PS2_DEV_IDENTIFY);
	id = ps2_read(PS2_DATA);
	ps2_clear_buffer();

	/* enable */
	psaux_command_write(PS2_DEV_ENABLE);
}

void irq_psaux(int num, struct sigcontext *sc)
{
	unsigned char ch;

	ch = inport_b(PS2_DATA);

	/* aux controller said 'acknowledge!' */
	if(ch == DEV_ACK) {
		ack = 1;
	}
	/* decode the byte stream into normalized pointer events; the
	 * decoder keeps its own packet framing and drops strays */
	mousedev_ps2_byte(ch);
}

void psaux_init(void)
{
	int errno;
	int irq_registered = 0;

	/* reset device */
	psaux_command_write(PS2_DEV_RESET);
	if((errno = ps2_read(PS2_DATA)) != DEV_RESET_OK) {
		printk("psaux     0x%04x-0x%04x       \tdevice not detected\n", 0x60, 0x64);
		return;
	}
	if(ps2_read(PS2_DATA) == 0) {
		is_ps2 = 1;
	}

	/* Register the handler but keep IRQ12 MASKED until the polling
	 * init below completes (the same race as the keyboard: on the
	 * 64-bit port the IRQ is dispatched fast enough that irq_psaux
	 * consumes the init response bytes before ps2_read() can, breaking
	 * the enable handshake and leaving the mouse silent). Unmasked at
	 * the end of the function. */
	irq_registered = !register_irq(PSAUX_IRQ, &irq_config_psaux);

	ps2_clear_buffer();
	psaux_identify();
	printk("psaux     0x%04x-0x%04x    %d", 0x60, 0x64, PSAUX_IRQ);
	printk("\ttype=%s", is_ps2 ? "PS/2" : "unknown");
	switch(id) {
		case -1:
			printk(", unknown ID %x", id & 0xFF);
			break;
		case 0:
			printk(", standard mouse");
			break;
		case 2:
			printk(", track ball");
			break;
		case 3:
			printk(", 3-button wheel mouse");
			break;
		case 4:
			printk(", 5-button wheel mouse");
			break;
		default:
			printk(", unknown mouse");
			break;
	}
	printk("\n");

	/* the PS/2 mouse exists: publish its topology node and the
	 * by-role alias the session opens (a USB mouse publishes
	 * USB/Mouse and creates the alias only if this one did not) */
	devfs_make_node("PS2/Mouse", MKDEV(MOUSE_MAJOR, MOUSE_MINOR), S_IFCHR | S_IRUSR | S_IWUSR);
	devfs_make_symlink("mouse", "PS2/Mouse", 0777);

	/* init complete; unmask IRQ12 so motion packets start flowing */
	if(irq_registered) {
		enable_irq(PSAUX_IRQ);
	}
}
#endif /* CONFIG_PSAUX */
