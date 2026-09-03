/*
 * fnx/drivers/char/keyboard.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/limits.h>
#include <fnx/kparms.h>
#include <fnx/ps2.h>
#include <fnx/serial.h>
#include <fnx/keyboard.h>
#include <fnx/kbdaux.h>
#include <fnx/reboot.h>
#include <fnx/console.h>
#include <fnx/vgacon.h>
#include <fnx/pic.h>
#include <fnx/irq.h>
#include <fnx/signal.h>
#include <fnx/process.h>
#include <fnx/sleep.h>
#include <fnx/kd.h>
#include <fnx/sysrq.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#define DELAY_250	0x00	/* typematic delay at 250ms (default) */
#define DELAY_500	0x40	/* typematic delay at 500ms */
#define DELAY_750	0x80	/* typematic delay at 750ms */
#define DELAY_1000	0xC0	/* typematic delay at 1000ms */
#define RATE_30		0x00	/* typematic rate at 30.0 reports/sec (default) */

#define EXTKEY		0xE0	/* extended key (AltGr, Ctrl-Print, etc.) */

__key_t *keymap_line;

static unsigned char e0_keys[128] = {
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x00-0x07 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x08-0x0F */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x10-0x17 */
	0, 0, 0, 0, E0ENTER, RCTRL, 0, 0,		/* 0x18-0x1F */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x20-0x27 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x28-0x2F */
	0, 0, 0, 0, 0, E0SLASH, 0, 0,			/* 0x30-0x37 */
	ALTGR, 0, 0, 0, 0, 0, 0, 0,			/* 0x38-0x3F */
	0, 0, 0, 0, 0, 0, 0, E0HOME,			/* 0x40-0x47 */
	E0UP, E0PGUP, 0, E0LEFT, 0, E0RIGHT, 0, E0END,	/* 0x48-0x4F */
	E0DOWN, E0PGDN, E0INS, E0DEL, 0, 0, 0, 0,	/* 0x50-0x57 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x58-0x5f */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x60-0x67 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x68-0x6F */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 0x70-0x77 */
	0, 0, 0, 0, 0, 0, 0, 0				/* 0x78-0x7F */
};

static unsigned char leds = 0;
static unsigned char shift = 0;
static unsigned char altgr = 0;
static unsigned char ctrl = 0;
static unsigned char alt = 0;
static unsigned char extkey = 0;
static unsigned char deadkey = 0;
static unsigned char altsysrq = 0;
static int sysrq_op = 0;
static unsigned char kb_identify[2] = {0, 0};
static unsigned char is_ps2 = 0;
static unsigned char orig_scan_set = 0;

static void process_scancode(unsigned char scode, int is_ext);
volatile unsigned char ack = 0;

static char do_switch_console = -1;
static unsigned char do_buf_scroll = 0;
static unsigned char do_setleds = 0;
static unsigned char do_tty_stop = 0;
static unsigned char do_tty_start = 0;
static unsigned char do_sysrq = 0;

char ctrl_alt_del = 1;
char any_key_to_reboot = 0;

static struct bh keyboard_bh = { 0, &irq_keyboard_bh, NULL };
static struct interrupt irq_config_keyboard = { 0, "keyboard", &irq_keyboard, NULL };
static struct tty *kbd_target_tty(void);
static struct vconsole *kbd_vc(void);

struct diacritic *diacr;
static char *diacr_chars = "`'^~\"";
struct diacritic grave_table[NR_DIACR] = {
	{ 'A', '\300' },
	{ 'E', '\310' },
	{ 'I', '\314' },
	{ 'O', '\322' },
	{ 'U', '\331' },
	{ 'a', '\340' },
	{ 'e', '\350' },
	{ 'i', '\354' },
	{ 'o', '\362' },
	{ 'u', '\371' },
};
struct diacritic acute_table[NR_DIACR] = {
	{ 'A', '\301' },
	{ 'E', '\311' },
	{ 'I', '\315' },
	{ 'O', '\323' },
	{ 'U', '\332' },
	{ 'a', '\341' },
	{ 'e', '\351' },
	{ 'i', '\355' },
	{ 'o', '\363' },
	{ 'u', '\372' },
};
struct diacritic circm_table[NR_DIACR] = {
	{ 'A', '\302' },
	{ 'E', '\312' },
	{ 'I', '\316' },
	{ 'O', '\324' },
	{ 'U', '\333' },
	{ 'a', '\342' },
	{ 'e', '\352' },
	{ 'i', '\356' },
	{ 'o', '\364' },
	{ 'u', '\373' },
};
struct diacritic tilde_table[NR_DIACR] = {
	{ 'A', '\303' },
	{ 'N', '\321' },
	{ 'O', '\325' },
	{ 'a', '\343' },
	{ 'n', '\361' },
	{ 'o', '\365' },
};
struct diacritic diere_table[NR_DIACR] = {
	{ 'A', '\304' },
	{ 'E', '\313' },
	{ 'I', '\317' },
	{ 'O', '\326' },
	{ 'U', '\334' },
	{ 'a', '\344' },
	{ 'e', '\353' },
	{ 'i', '\357' },
	{ 'o', '\366' },
	{ 'u', '\374' },
};

static char *pad_chars = "0123456789+-*/\015,.";

static char *pad_seq[] = {
	"\033[2~",	/* INS */
	"\033[4~",	/* END */
	"\033[B" ,	/* DOWN */
	"\033[6~",	/* PGDN */
	"\033[D" ,	/* LEFT */
	"\033[G" ,	/* MID */
	"\033[C" ,	/* RIGHT */
	"\033[1~",	/* HOME */
	"\033[A" ,	/* UP */
	"\033[5~",	/* PGUP */
	"+",		/* PLUS */
	"-",		/* MINUS */
	"*",		/* ASTERISK */
	"/",		/* SLASH */
	"'\n'",		/* ENTER */
	",",		/* COMMA */
	"\033[3~",	/* DEL */
};

static char *fn_seq[] = {
	"\033[[A",	/* F1 */
	"\033[[B",	/* F2 */
	"\033[[C",	/* F3 */
	"\033[[D",	/* F4 */
	"\033[[E",	/* F5 */
	"\033[17~",	/* F6 */
	"\033[18~",	/* F7 */
	"\033[19~",	/* F8 */
	"\033[20~",	/* F9 */
	"\033[21~",	/* F10 */
	"\033[23~",	/* F11, SF1 */
	"\033[24~",	/* F12, SF2 */
	"\033[25~",	/* SF3 */
	"\033[26~",	/* SF4 */
	"\033[28~",	/* SF5 */
	"\033[29~",	/* SF6 */
	"\033[31~",	/* SF7 */
	"\033[32~",	/* SF8 */
	"\033[33~",	/* SF9 */
	"\033[34~",	/* SF10 */
};

static void keyboard_identify(void)
{
	char config;

	/* disable */
	ps2_write(PS2_DATA, PS2_KB_DISABLE);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on disable command!\n", __FUNCTION__);
	} else {
		is_ps2++;
	}

	/* identify */
	ps2_write(PS2_DATA, PS2_DEV_IDENTIFY);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on identify command!\n", __FUNCTION__);
	} else {
		is_ps2++;
	}
	kb_identify[0] = ps2_read(PS2_DATA);
	kb_identify[1] = ps2_read(PS2_DATA);

	/* get scan code */
	ps2_write(PS2_COMMAND, PS2_CMD_RECV_CONFIG);
	config = ps2_read(PS2_DATA);	/* save state */
	ps2_write(PS2_COMMAND, PS2_CMD_SEND_CONFIG);
	ps2_write(PS2_DATA, config & ~0x40);	/* unset translation */
	ps2_write(PS2_DATA, PS2_KB_GETSETSCAN);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on get scan code command!\n", __FUNCTION__);
	}
	ps2_write(PS2_DATA, 0);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on get scan code command!\n", __FUNCTION__);
	}
	orig_scan_set = ps2_read(PS2_DATA);
	if(orig_scan_set != 2) {
		ps2_write(PS2_DATA, PS2_KB_GETSETSCAN);
		ps2_write(PS2_DATA, 2);
		if(ps2_wait_ack()) {
			printk("WARNING: %s(): ACK not received on set scan code command!\n", __FUNCTION__);
		}
	}
	ps2_write(PS2_COMMAND, PS2_CMD_SEND_CONFIG);
	ps2_write(PS2_DATA, config);	/* restore state */

	/* enable */
	ps2_write(PS2_DATA, PS2_DEV_ENABLE);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on enable command!\n", __FUNCTION__);
	}
	ps2_clear_buffer();
}

static void putc(struct tty *tty, unsigned char ch)
{
	if(tty->count) {
		if(charq_putchar(&tty->read_q, ch) < 0) {
			if(tty->termios.c_iflag & IMAXBEL) {
				vconsole_beep();
			}
		}
	}
}

static void puts(struct tty *tty, char *seq)
{
	char ch;

	if(tty->count) {
		while((ch = *(seq++))) {
			putc(tty, ch);
		}
	}
}

void set_leds(unsigned char led_status)
{
	ps2_write(PS2_DATA, PS2_KB_SETLED);
	ps2_wait_ack();

	ps2_write(PS2_DATA, led_status);
	ps2_wait_ack();
}

void irq_keyboard(int num, struct sigcontext *sc)
{
	unsigned char scode;

	struct tty *tty;
	struct vconsole *vc;

	tty = kbd_target_tty();
	vc = kbd_vc();
	(void)vc;
	if(!tty) {
		/* no console at all (vconsoles disabled and no serial):
		 * nothing to feed */
		inport_b(PS2_DATA);
		return;
	}

	scode = inport_b(PS2_DATA);

	/* keyboard controller said 'acknowledge!' */
	if(scode == DEV_ACK) {
		ack = 1;
		return;
	}

	keyboard_bh.flags |= BH_ACTIVE;

	/* if in pure raw mode just queue the scan code and return */
	if(tty->kbd.mode == K_RAW) {
		putc(tty, scode);
		return;
	}

	if(scode == EXTKEY) {
		extkey = 1;
		return;
	}
	
	process_scancode(scode, extkey);
	extkey = 0;
}
/* keyboard input goes to the active console: the serial tty when the
 * system console is a serial device (headless boots), else the current
 * virtual console (desktop framebuffer boots) */
static struct tty *kbd_target_tty(void)
{
	struct tty *tty;

	if(kparms.syscondev && MAJOR(kparms.syscondev) == SERIAL_MAJOR) {
		if((tty = get_tty(kparms.syscondev))) {
			return tty;
		}
	}
	return get_tty(MKDEV(VCONSOLES_MAJOR, current_cons));
}

/* the current virtual console, or a static zeroed fallback when virtual
 * consoles are disabled (serial-only console): key state (caps/num/scroll)
 * still works, the cooked output goes to kbd_target_tty() */
static struct vconsole kbd_dummy_vc;

static struct vconsole *kbd_vc(void)
{
	struct tty *tty;

	tty = get_tty(MKDEV(VCONSOLES_MAJOR, current_cons));
	if(tty) {
		return (struct vconsole *)tty->driver_data;
	}
	return &kbd_dummy_vc;
}

/* Normalize a resolved key (after the keymap lookup) into a /dev/kbd
 * keysym + modifiers and emit it. Returns 1 when an event was queued. */
static int kbdaux_gui_emit(__key_t key, int vc_capslock, int vc_numlock)
{
	static const unsigned char pad_sem[10] = {
		KB_KEY_INS, KB_KEY_END, KB_KEY_DOWN, KB_KEY_PGDN, KB_KEY_LEFT,
		0,		KB_KEY_RIGHT, KB_KEY_HOME, KB_KEY_UP,
		KB_KEY_PGUP
	};
	int type = key & 0xFF00;
	int c = key & 0xFF;
	int nkey = -1;
	unsigned char mods = 0;

	if(shift) {
		mods |= KB_MOD_SHIFT;
	}
	if(ctrl) {
		mods |= KB_MOD_CTRL;
	}
	if(alt) {
		mods |= KB_MOD_ALT;
	}
	if(altgr) {
		mods |= KB_MOD_ALTGR;
	}
	if(vc_capslock) {
		mods |= KB_MOD_CAPS;
	}
	if(vc_numlock) {
		mods |= KB_MOD_NUM;
	}

	switch(type) {
		case 0:			/* plain char / control */
		case LETTER_KEYS:
			if(c < 0x80) {
				nkey = c;
			}
			break;
		case META_KEYS:		/* Alt+key (mods.alt already set) */
			if(c < 0x80) {
				nkey = c;
			}
			break;
		case SPEC_KEYS:
			if(key == CR) {
				nkey = '\r';
			}
			break;
		case FN_KEYS:
			if(c <= 11) {
				nkey = KB_KEY_F1 + c;
			}
			break;
		case PAD_KEYS:
			if(c <= 9) {
				if(vc_numlock) {
					nkey = pad_chars[c];
				} else {
					nkey = pad_sem[c];
				}
			} else if(c == 16) {		/* ./Del key */
				nkey = vc_numlock ? '.' : KB_KEY_DEL;
			} else if(c >= 10 && c <= 15) {
				nkey = pad_chars[c];	/* + - * / CR , */
			}
			break;
		default:
			break;	/* DEAD/CONS keys: not GUI keys */
	}
	if(nkey < 0) {
		return 0;
	}
	kbdaux_event(nkey, mods, 1);
	return 1;
}

static void process_scancode(unsigned char scode, int is_ext)
{
	struct tty *tty;
	struct vconsole *vc;
	__key_t key, type;
	unsigned char c;
	int n;
	int mod;

	tty = kbd_target_tty();
	vc = kbd_vc();
	if(!tty) {
		return;
	}

	if(is_ext) {
		key = e0_keys[scode & 0x7F];
	} else {
		key = scode & 0x7F;
	}

	if(tty->kbd.mode == K_MEDIUMRAW) {
		putc(tty, key | (scode & 0x80));
		is_ext = 0;
		return;
	}

	key = keymap[NR_MODIFIERS * (scode & 0x7F)];

	/* bit 7 enabled means a key has been released */
	if(scode & NR_SCODES) {
		switch(key) {
			case CTRL:
			case LCTRL:
			case RCTRL:
				ctrl = 0;
				break;
			case ALT:
				if(!is_ext) {
					alt = 0;
					altsysrq = 0;
				} else {
					altgr = 0;
				}
				break;
			case SHIFT:
			case LSHIFT:
			case RSHIFT:
				if(!is_ext) {
					shift = 0;
				}
				break;
			case CAPS:
			case NUMS:
			case SCRL:
				leds = 0;
				break;
		}
		is_ext = 0;
		return;
	}

	switch(key) {
		case CAPS:
			if(!leds) {
				vc->led_status ^= CAPSBIT;
				vc->capslock = !vc->capslock;
				do_setleds = 1;
			}
			leds = 1;
			return;
		case NUMS:
			if(!leds) {
				vc->led_status ^= NUMSBIT;
				vc->numlock = !vc->numlock;
				do_setleds = 1;
			}
			leds = 1;
			return;
		case SCRL:
			if(!leds) {
				if(vc->scrlock) {
					do_tty_start = 1;
				} else {
					do_tty_stop = 1;
				}
			}
			leds = 1;
			return;
		case CTRL:
		case LCTRL:
		case RCTRL:
			ctrl = 1;
			return;
		case ALT:
			if(!is_ext) {
				alt = 1;
			} else {
				altgr = 1;
			}
			return;
		case SHIFT:
		case LSHIFT:
		case RSHIFT:
			shift = 1;
			is_ext = 0;
			return;
	}

	if(ctrl && alt && key == DEL) {
		if(ctrl_alt_del) {
			reboot();
		} else {
			send_sig(&proc_table[INIT], SIGINT);
		}
		return;
	}

	keymap_line = &keymap[(scode & 0x7F) * NR_MODIFIERS];
	mod = 0;

	if(vc->capslock && (keymap_line[MOD_BASE] & LETTER_KEYS)) {
		mod = !vc->capslock ? shift : vc->capslock - shift;
	} else {
		if(shift && !is_ext) {
			mod = 1;
		}
	}
	if(altgr) {
		mod = 2;
	}
	if(ctrl) {
		mod = 4;
	}
	if(alt) {
		mod = 8;
	}

	key = keymap_line[mod];

	if(key >= AF1 && key <= AF12) {
		do_switch_console = key - CONS_KEYS;
		return;
	}

	if(shift && (key == PGUP)) {
		do_buf_scroll = SCROLL_UP;
		return;
	}

	if(shift && (key == PGDN)) {
		do_buf_scroll = SCROLL_DOWN;
		return;
	}

	if(is_ext && (scode == SLASH_NPAD)) {
		key = SLASH;
	}

	if(any_key_to_reboot) {
		reboot();
	}


	type = key & 0xFF00;
	c = key & 0xFF;

	if(altsysrq) {
		/* treat 0-9 and a-z keys as normal */
		type &= ~META_KEYS;
	}

	/* the GUI keyboard (/dev/kbd): when the session compositor has it
	 * open it owns the keyboard - normalize the resolved key into a
	 * /dev/kbd event and skip the console emission entirely (no chars
	 * leak into the serial console, no dead-key/console state here) */
	if(kbdaux_active()) {
		kbdaux_gui_emit(key, vc->capslock, vc->numlock);
		return;
	}

	switch(type) {
		case FN_KEYS:
			if(c > sizeof(fn_seq) / sizeof(char *)) {
				printk("WARNING: %s(): unrecognized function key.\n", __FUNCTION__);
				break;
			}
			puts(tty, fn_seq[c]);
			break;

		case SPEC_KEYS:
			switch(key) {
				case CR:
					putc(tty, C('M'));
					break;
				case SYSRQ:
					altsysrq = 1;
					break;
			}
			break;

		case PAD_KEYS:
			if(!vc->numlock) {
				puts(tty, pad_seq[c]);
			} else {
				putc(tty, pad_chars[c]);
			}
			break;

		case DEAD_KEYS:
			if(!deadkey) {
				switch(c) {
					case GRAVE ^ DEAD_KEYS:
						deadkey = 1;
						diacr = grave_table;
						break;
					case ACUTE ^ DEAD_KEYS:
						deadkey = 2;
						diacr = acute_table;
						break;
					case CIRCM ^ DEAD_KEYS:
						deadkey = 3;
						diacr = circm_table;
						break;
					case TILDE ^ DEAD_KEYS:
						deadkey = 4;
						diacr = tilde_table;
						break;
					case DIERE ^ DEAD_KEYS:
						deadkey = 5;
						diacr = diere_table;
						break;
				}
				return;
			}
			c = diacr_chars[c];
			deadkey = 0;
			putc(tty, c);

			break;

		case META_KEYS:
			putc(tty, '\033');
			putc(tty, c);
			break;

		case LETTER_KEYS:
			if(deadkey) {
				for(n = 0; n < NR_DIACR; n++) {
					if(diacr[n].letter == c) {
						c = diacr[n].code;
					}
				}
			}
			putc(tty, c);
			break;

		default:
			if(altsysrq) {
				switch(c) {
					case 'l':
						sysrq_op = SYSRQ_STACK;
						do_sysrq = 1;
						break;
					case 'm':
						sysrq_op = SYSRQ_MEMORY;
						do_sysrq = 1;
						break;
					case 't':
						sysrq_op = SYSRQ_TASKS;
						do_sysrq = 1;
						break;
					default:
						sysrq_op = SYSRQ_UNDEF;
						do_sysrq = 1;
						break;
				}
				break;
			}
			if(deadkey && c == ' ') {
				c = diacr_chars[deadkey - 1];
			}
			putc(tty, c);
			break;
	}

	deadkey = 0;
}


void irq_keyboard_bh(struct sigcontext *sc)
{
	struct tty *vtty, *tty;
	struct vconsole *vc;
	char value;

	vtty = get_tty(MKDEV(VCONSOLES_MAJOR, current_cons));
	vc = kbd_vc();

	if(video.screen_on) {
		video.screen_on(vc);
	}

	/* console switching + scrollback need a real virtual console; with
	 * vconsoles disabled (serial-only, compositor owns the display)
	 * those keys are inert. */
	if(vtty && vc) {
		if(do_switch_console >= 0) {
			value = do_switch_console;
			do_switch_console = -1;
			vconsole_select(value);
		}

		if(do_buf_scroll) {
			value = do_buf_scroll;
			do_buf_scroll = 0;
			video.buf_scroll(vc, value);
		}
	}

	/* keyboard LEDs track the lock state even without a video console */
	if(do_setleds && vc) {
		do_setleds = 0;
		set_leds(vc->led_status);
	}

	/* scroll-lock flow control applies to the output tty (the serial
	 * console when vconsoles are disabled) */
	tty = kbd_target_tty();
	if(!tty) {
		return;
	}
	if(do_tty_start) {
		do_tty_start = do_tty_stop = 0;
		tty->start(tty);
	}

	if(do_tty_stop) {
		do_tty_start = do_tty_stop = 0;
		tty->stop(tty);
	}

	if(do_sysrq) {
		do_sysrq = 0;
		sysrq(sysrq_op);
	}

	for(tty = tty_table; tty; tty = tty->next) {
		if(MAJOR(tty->dev) == VCONSOLES_MAJOR && MINOR(tty->dev) < NR_VCONSOLES) {
			if(!tty->read_q.count) {
				continue;
			}
			if(tty->kbd.mode == K_RAW || tty->kbd.mode == K_MEDIUMRAW) {
				wakeup(&tty->read_q);
				continue;
			}
			if(!can_lock_area(AREA_TTY_READ)) {
				keyboard_bh.flags |= BH_ACTIVE;
				continue;
			}
			tty->input(tty);
			unlock_area(AREA_TTY_READ);
		}
	}
}


/* Public seam: feed set-1 scancodes from a non-PS/2 source (e.g. USB
 * HID keyboard). is_ext marks E0-prefixed keys. */
void kbd_process_scancode(unsigned char scode, int is_ext)
{
	struct tty *tty;

	process_scancode(scode, is_ext);
	/* wake the console tty readers (the PS/2 ISR path does this via
	 * the keyboard bottom-half; a non-PS/2 keyboard must too) */
	tty = kbd_target_tty();
	if(MAJOR(tty->dev) == SERIAL_MAJOR) {
		/* mirror the serial ISR: cook the read queue (canonical
		 * processing) and wake the reader */
		if(can_lock_area(AREA_SERIAL_READ)) {
			tty->input(tty);
			unlock_area(AREA_SERIAL_READ);
		} else {
			printk("kbd: serial lock busy, chars queued\n");
		}
	} else {
		keyboard_bh.flags |= BH_ACTIVE;
		add_bh(&keyboard_bh);
	}
}

void keyboard_init(void)
{
	struct tty *tty;
	struct vconsole *vc;
	int errno, irq_ok;

	/* virtual consoles may be disabled (serial-only console): the
	 * keyboard then has no vc to drive, so skip the video hooks */
	tty = get_tty(MKDEV(VCONSOLES_MAJOR, current_cons));
	if(tty) {
		vc = (struct vconsole *)tty->driver_data;
		if(video.screen_on) {
			video.screen_on(vc);
		}
		if(video.cursor_blink) {
			video.cursor_blink((addr_t)vc);
		}
	}

	add_bh(&keyboard_bh);
	/* Register the handler but keep IRQ1 MASKED until the polling-based
	 * init below completes. On the 64-bit port the IRQ is dispatched fast
	 * enough that irq_keyboard consumes the init response bytes before
	 * ps2_wait_ack()/ps2_read() can read them, breaking the init. */
	irq_ok = register_irq(KEYBOARD_IRQ, &irq_config_keyboard);

	/* reset device */
	ps2_write(PS2_DATA, PS2_DEV_RESET);
	if(ps2_wait_ack()) {
		printk("WARNING: %s(): ACK not received on reset command!\n", __FUNCTION__);
	}
	if((errno = ps2_read(PS2_DATA)) != DEV_RESET_OK) {
		/* some keyboards return an ID byte before 0xAA */
		if((errno = ps2_read(PS2_DATA)) != DEV_RESET_OK) {
			printk("WARNING: %s(): keyboard returned 0x%x on reset (1).\n", __FUNCTION__, errno);
		}
	}

	ps2_clear_buffer();
	keyboard_identify();
	printk("keyboard  0x%04x-0x%04x     %d", 0x60, 0x64, KEYBOARD_IRQ);
	printk("\ttype=%s %s", kb_identify[0] == 0xAB ? "MF2" : "unknown", is_ps2 ? "PS/2" : "");
	printk(" %s", (kb_identify[1] == 0x41 || kb_identify[1] == 0xC1) ? "translated" : "");
	printk(" scan set 2");
	if(orig_scan_set != 2) {
		printk(" (was %d)", orig_scan_set);
	}
	printk("\n");

	ps2_write(PS2_DATA, PS2_DEV_RATE);
	ps2_wait_ack();
	ps2_write(PS2_DATA, DELAY_250 | RATE_30);
	ps2_wait_ack();

	/* init complete; unmask IRQ1 so key presses start flowing again */
	if(!irq_ok) {
		enable_irq(KEYBOARD_IRQ);
	}
}
