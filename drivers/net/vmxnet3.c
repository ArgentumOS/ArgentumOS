/*
 * vmxnet3.c - VMware Paravirtualized Ethernet v3 (vmxnet3) NIC driver for FNX.
 *
 * QEMU's vmxnet3 (PCI 15AD:07B0) behind the ext_* API. A paravirtual
 * NIC with a minimal register file: BAR1 (VD) has the device command
 * registers, BAR0 (PT) the interrupt mask + the TX/RX producer doorbells.
 * All descriptor/ring state lives in a "driver shared" page in guest RAM
 * (magic 0xbabefee1) plus a queue-descriptor table, both pointed to from
 * BAR1. The device DMA's frames into RX buffers and reads TX buffers from
 * guest RAM; each queue has a data ring + a completion ring, synchronized
 * with the generation bit (flips on index wrap, starts at 1).
 *
 * Cheat sheet (QEMU hw/net/vmxnet3.c, vmxnet3.h):
 * - BAR1 (VD, 0x0-0x40): VRRS=0x00, UVRS=0x08, DSAL=0x10 (shared PA low),
 *   DSAH=0x18 (high), CMD=0x20, MACL=0x28 / MACH=0x30, ICR=0x38
 *   (interrupt cause - a READ clears the INTx line), ECR=0x40.
 * - BAR0 (PT): IMR=0x00 (per-interrupt mask, 8-byte stride; write 0 to
 *   unmask), TXPROD=0x600 (write kicks TX queue 0), RXPROD=0x800 /
 *   RXPROD2=0xA00 (no-ops in QEMU; RX is fully descriptor-driven).
 * - CMD values: 0xCAFE0000 ACTIVATE_DEV (reads the shared page + queue
 *   table and starts the device), 0xCAFE0001 QUIESCE, 0xCAFE0002 RESET,
 *   0xCAFE0003 UPDATE_RX_MODE, 0xF00D0002 GET_LINK.
 * - DriverShared (offset 0x08 = devRead.misc): driverInfo (version=1,
 *   gos, vmxnet3RevSpt=1, uptVerSpt=1), uptFeatures, ddPA, queueDescPA,
 *   ddLen, queueDescLen, mtu (1500), maxNumRxSG (1), numTxQueues (1),
 *   numRxQueues (1). 0x50 = intrConf (autoMask=0, numIntrs=1,
 *   eventIntrIdx=0), 0x78 = rxFilterConf (rxMode UCAST|MCAST|BCAST,
 *   mfTableLen=0, vfTable zeros), 0x138 = ecr.
 * - Queue table: one Vmxnet3_TxQueueDesc (128B) + one Vmxnet3_RxQueueDesc
 *   (128B), 128-aligned. TX conf: txRingBasePA@0x10, compRingBasePA@0x20,
 *   txRingSize@0x38, compRingSize@0x40, intrIdx@0x48. RX conf:
 *   rxRingBasePA[0]@0x10, compRingBasePA@0x20, rxRingSize[0]@0x38,
 *   compRingSize@0x40, intrIdx@0x48.
 * - TX desc (16B): addr@0, val1@8 = len(14) | gen(1)<<14 | dtype(1)<<16,
 *   val2@12 = hlen(10) | om(2)<<10 | eop(1)<<12 | cq(1)<<13. Simplified
 *   path: one desc per frame (om=0, hlen=0, eop=1, cq=1). The device
 *   reads the frame from addr and writes a TxCompDesc (val1 = txdIdx,
 *   val2 bit31 = gen) to the comp ring.
 * - RX desc (16B): addr@0, val1@8 = len(14) | btype(1)<<14 | gen(1)<<31.
 *   btype=0 (head). The device DMA's the frame into addr and writes an
 *   RxCompDesc (val1 bits 0-11 = rxdIdx, val2 bits 0-13 = len, val3
 *   bit 31 = gen). The driver refills the RX desc after dequeue.
 * - Interrupts: INTx only (we never enable MSI-X); reading ICR clears
 *   the line. Ring gen starts at 1 on both sides and flips on wrap;
 *   the driver writes data rings and reads comp rings.
 */

#include <fnx/limits.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/stdio.h>
#include <fnx/irq.h>
#include <fnx/asm.h>
#include <fnx/sleep.h>
#include <fnx/pci.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/time.h>
#include <fnx/net/ext_net.h>

#define VMXNET3_VENDOR		0x15AD	/* VMware */
#define VMXNET3_DEVICE		0x07B0

/* BAR1 (VD) registers */
#define VD_VRRS		0x00
#define VD_UVRS		0x08
#define VD_DSAL		0x10
#define VD_DSAH		0x18
#define VD_CMD		0x20
#define VD_MACL		0x28
#define VD_MACH		0x30
#define VD_ICR		0x38
#define VD_ECR		0x40

/* BAR0 (PT) registers */
#define PT_IMR		0x00
#define PT_TXPROD	0x600

#define CMD_ACTIVATE_DEV	0xCAFE0000

#define VMXNET3_REV1_MAGIC	0xbabefee1

#define RX_RING_ENTRIES	64
#define RX_BUF_SIZE	2048
#define TX_RING_ENTRIES	64
#define TX_MAX_FRAME	1518

/* generation bit: starts at 1, flips on index wrap */
#define TXD_GEN_SHIFT	14
#define TXD_EOP_SHIFT	12
#define TXD_CQ_SHIFT	13
#define RXD_GEN_SHIFT	31
#define RXD_BTYPE_SHIFT	14
#define TCD_GEN_SHIFT	31
#define RCD_GEN_SHIFT	31
#define RCD_LEN_MASK	0x3FFF
#define RCD_RXDIDX_MASK	0x0FFF

struct vmxnet3_device {
	int present;
	unsigned long mmio0;	/* BAR0 (PT) kernel VA */
	unsigned long mmio1;	/* BAR1 (VD) kernel VA */
	unsigned char irq;
	unsigned char mac[6];
	/* driver shared page + queue table in the low DMA window */
	unsigned long shared_phys;
	unsigned long qdesc_phys;
	/* TX ring: one 4K page each for data + comp */
	unsigned long tx_ring_phys;
	unsigned long tx_comp_phys;
	unsigned long tx_buf_phys[TX_RING_ENTRIES];
	unsigned int tx_cur;	/* next TX data slot */
	unsigned int tx_gen;	/* TX data ring gen (0/1) */
	unsigned int tx_comp_cur;	/* next TX comp slot to reap */
	unsigned int tx_comp_gen;
	/* RX ring: data + comp pages, one buffer page per slot */
	unsigned long rx_ring_phys;
	unsigned long rx_comp_phys;
	unsigned long rx_buf_phys[RX_RING_ENTRIES];
	unsigned int rx_fill;	/* next RX data slot to fill */
	unsigned int rx_fill_gen;
	unsigned int rx_comp_cur;	/* next RX comp slot to reap */
	unsigned int rx_comp_gen;
	int rx_wait;
};

#define VMXNET3_MMIO0_VA	0xFFFFBF0000000000UL	/* pml4[508] */
#define VMXNET3_MMIO1_VA	0xFFFFBF8000000000UL	/* pml4[509] */
#define VMXNET3_MMIO_SIZE	0x1000
extern int map_page64(unsigned long, unsigned long, unsigned long);
extern unsigned long alloc_pages64(int);
extern void free_pages64(unsigned long, int);

static struct vmxnet3_device vmxnet3;

static struct ext_net_ops vmxnet3_ops;
static int vmxnet3_recvfrom(void *, __size_t);

static unsigned int vmxnet3_rl(unsigned long base, unsigned short off)
{
	return *(volatile unsigned int *)(base + off);
}
static void vmxnet3_wl(unsigned long base, unsigned short off, unsigned int val)
{
	*(volatile unsigned int *)(base + off) = val;
}

static unsigned long vmxnet3_alloc_dma_page(void)
{
	unsigned long p;
	int n;

	for(n = 0; n < 64; n++) {
		p = alloc_pages64(1);
		if(p >= 0x100000 && p < 0x1000000) {
			return p;
		}
		if(p) {
			free_pages64(p, 1);
		}
	}
	return 0;
}

/* ---- driver shared page + queue table (raw little-endian words) ---- */
static void vmxnet3_put32(unsigned long pa, unsigned int off, unsigned int v)
{
	*(volatile unsigned int *)(P2V(pa) + off) = v;
}
static void vmxnet3_put64(unsigned long pa, unsigned int off, unsigned long v)
{
	*(volatile unsigned long *)(P2V(pa) + off) = v;
}

/* the RX descriptor: addr@0, val1@8 = len|btype<<14|gen<<31 */
static void vmxnet3_rx_fill_desc(unsigned int idx)
{
	volatile unsigned int *d =
		(volatile unsigned int *)P2V(vmxnet3.rx_ring_phys + idx * 16);
	unsigned int gen = vmxnet3.rx_fill_gen ? (1 << RXD_GEN_SHIFT) : 0;

	d[0] = (unsigned int)vmxnet3.rx_buf_phys[idx];
	d[1] = 0;
	d[2] = RX_BUF_SIZE | gen;	/* btype = 0 (head) */
	d[3] = 0;
	__asm__ __volatile__("" ::: "memory");
}

static int vmxnet3_rx_pending(void)
{
	volatile unsigned int *d;

	if(!vmxnet3.present) {
		return 0;
	}
	d = (volatile unsigned int *)P2V(vmxnet3.rx_comp_phys +
					  vmxnet3.rx_comp_cur * 16);
	return ((d[3] >> RCD_GEN_SHIFT) & 1) == vmxnet3.rx_comp_gen;
}

static int vmxnet3_rx_dequeue(void *buffer, __size_t count)
{
	volatile unsigned int *d;
	unsigned int idx, len, n = 0;

	d = (volatile unsigned int *)P2V(vmxnet3.rx_comp_phys +
					  vmxnet3.rx_comp_cur * 16);
	if(((d[3] >> RCD_GEN_SHIFT) & 1) != vmxnet3.rx_comp_gen) {
		return -EAGAIN;
	}
	idx = d[0] & RCD_RXDIDX_MASK;
	len = d[2] & RCD_LEN_MASK;
	if(idx >= RX_RING_ENTRIES || len < 14 || len > RX_BUF_SIZE) {
		goto skip;
	}
	n = (len < count) ? len : count;
	memcpy_b(buffer, (void *)P2V(vmxnet3.rx_buf_phys[idx]), n);
skip:
	/* advance the comp ring (flip gen on wrap) */
	if(++vmxnet3.rx_comp_cur >= RX_RING_ENTRIES) {
		vmxnet3.rx_comp_cur = 0;
		vmxnet3.rx_comp_gen ^= 1;
	}
	/* refill the consumed RX data slot (flip its gen on wrap) */
	vmxnet3_rx_fill_desc(vmxnet3.rx_fill);
	if(++vmxnet3.rx_fill >= RX_RING_ENTRIES) {
		vmxnet3.rx_fill = 0;
		vmxnet3.rx_fill_gen ^= 1;
	}
	return (idx < RX_RING_ENTRIES && len >= 14 && len <= RX_BUF_SIZE) ? n : -EAGAIN;
}

static int vmxnet3_tx_send(const void *frame, unsigned int len)
{
	volatile unsigned int *d, *c;
	unsigned int slot, spin, gen;

	if(!vmxnet3.present) {
		return -ENODEV;
	}
	if(len > TX_MAX_FRAME) {
		return -EMSGSIZE;
	}
	/* wait for the TX data slot to be free (gen not yet written back) */
	slot = vmxnet3.tx_cur % TX_RING_ENTRIES;
	d = (volatile unsigned int *)P2V(vmxnet3.tx_ring_phys + slot * 16);
	/* the previous send of this slot reaps its comp before reuse, so the
	 * slot is free once the comp ring has passed it - poll the comp ring */
	(void)d;
	gen = vmxnet3.tx_gen ? (1 << TXD_GEN_SHIFT) : 0;

	memcpy_b((void *)P2V(vmxnet3.tx_buf_phys[slot]), frame, len);
	d[0] = (unsigned int)vmxnet3.tx_buf_phys[slot];
	d[1] = 0;
	d[2] = (len & 0x3FFF) | gen;	/* dtype = 0 */
	d[3] = (1 << TXD_EOP_SHIFT) | (1 << TXD_CQ_SHIFT);
	__asm__ __volatile__("" ::: "memory");

	/* kick TX queue 0 */
	vmxnet3_wl(vmxnet3.mmio0, PT_TXPROD, vmxnet3.tx_cur + 1);

	/* wait for the completion (comp ring, gen matches) */
	/* TX comp: val1=txdIdx@dword0, val2=gen@dword3 bit 31 */
	c = (volatile unsigned int *)P2V(vmxnet3.tx_comp_phys +
					  vmxnet3.tx_comp_cur * 16);
	for(spin = 0; spin < 100000; spin++) {
		if(((c[3] >> TCD_GEN_SHIFT) & 1) == vmxnet3.tx_comp_gen) {
			break;
		}
	}
	if(spin >= 100000) {
		return -EAGAIN;
	}
	/* advance data + comp rings (flip gens on wrap) */
	vmxnet3.tx_cur++;
	if(vmxnet3.tx_cur % TX_RING_ENTRIES == 0) {
		vmxnet3.tx_gen ^= 1;
	}
	if(++vmxnet3.tx_comp_cur >= TX_RING_ENTRIES) {
		vmxnet3.tx_comp_cur = 0;
		vmxnet3.tx_comp_gen ^= 1;
	}
	return len;
}

static void vmxnet3_irq_handler(int num, struct sigcontext *sc)
{
	(void)num; (void)sc;
	if(!vmxnet3.present) {
		return;
	}
	/* a read of ICR clears the INTx line */
	if(vmxnet3_rl(vmxnet3.mmio1, VD_ICR)) {
		wakeup(&vmxnet3.rx_wait);
	}
}

static int vmxnet3_ext_recvfrom(int fd, void *buffer, __size_t count, struct sockaddr *addr, int *addrlen)
{
	(void)fd; (void)addr; (void)addrlen;
	return vmxnet3_recvfrom(buffer, count);
}

static int vmxnet3_recvfrom(void *buffer, __size_t count)
{
	extern unsigned int tv2ticks(const struct timeval *);
	struct timeval tv;
	unsigned int spin, woken;
	int n;

	for(;;) {
		n = vmxnet3_rx_dequeue(buffer, count);
		if(n >= 0) {
			return n;
		}
		for(spin = 0; spin < 10000; spin++) {
			if(vmxnet3_rx_pending()) {
				break;
			}
		}
		if(vmxnet3_rx_pending()) {
			continue;
		}
		if(!current->timeout) {
			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&vmxnet3.rx_wait, PROC_INTERRUPTIBLE);
			if(!current->timeout) {
				current->timeout = 1;
			}
			if(woken) {
				return -EINTR;
			}
		} else {
			return -EAGAIN;
		}
	}
}

static int vmxnet3_poll(void)
{
	return vmxnet3_rx_pending();
}

static int vmxnet3_ext_poll(int fd, int flag)
{
	(void)fd; (void)flag;
	return vmxnet3_poll();
}

static int vmxnet3_ext_ioctl(int fd, int request, void *arg)
{
	(void)fd;
	return -EOPNOTSUPP;
}

/* the remaining ops mirror the pcnet/ne2k wrappers */
static int vmxnet3_ext_open(int fd, int flags, int mode) { (void)fd; (void)flags; (void)mode; return 0; }
static int vmxnet3_ext_close(int fd) { (void)fd; return 0; }
static int vmxnet3_ext_bind(int fd, const struct sockaddr *addr, int len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int vmxnet3_ext_listen(int fd, int backlog) { (void)fd; (void)backlog; return -EOPNOTSUPP; }
static int vmxnet3_ext_connect(int fd, const struct sockaddr *addr, int len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int vmxnet3_ext_accept(int fd, struct sockaddr *addr, unsigned int *len) { (void)fd; (void)addr; (void)len; return -EOPNOTSUPP; }
static int vmxnet3_ext_sendto(int fd, const void *buf, __size_t len, const struct sockaddr *addr, int alen)
{
	(void)fd; (void)addr; (void)alen;
	return vmxnet3_tx_send(buf, len);
}
static int vmxnet3_ext_read(int fd, void *buf, __size_t len) { (void)fd; return vmxnet3_recvfrom(buf, len); }
static int vmxnet3_ext_write(int fd, const void *buf, __size_t len) { (void)fd; return vmxnet3_tx_send(buf, len); }

struct ext_net_ops *vmxnet3_probe(void)
{
	struct pci_device *pd;
	unsigned long mmio0, mmio1;
	unsigned int macl, mach, i;

	vmxnet3.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == VMXNET3_VENDOR &&
		   pd->device_id == VMXNET3_DEVICE) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return NULL;
	}

	mmio0 = pd->bar[0] & 0xFFFFFFF0;
	mmio1 = pd->bar[1] & 0xFFFFFFF0;
	if(!mmio0 || !mmio1) {
		return NULL;
	}
	/* map BAR0 (PT) and BAR1 (VD) into fixed kernel VAs */
	for(i = 0; i < VMXNET3_MMIO_SIZE / 4096; i++) {
		if(map_page64(VMXNET3_MMIO0_VA + i * 4096, mmio0 + i * 4096, 0x003) ||
		   map_page64(VMXNET3_MMIO1_VA + i * 4096, mmio1 + i * 4096, 0x003)) {
			return NULL;
		}
	}
	vmxnet3.mmio0 = VMXNET3_MMIO0_VA;
	vmxnet3.mmio1 = VMXNET3_MMIO1_VA;
	vmxnet3.irq = pd->irq;
	pci_write_short(pd, 0x04, 0x0006);	/* MEM | MASTER */

	/* the MAC is readable directly from BAR1 */
	macl = vmxnet3_rl(vmxnet3.mmio1, VD_MACL);
	mach = vmxnet3_rl(vmxnet3.mmio1, VD_MACH);
	vmxnet3.mac[0] = macl & 0xFF;
	vmxnet3.mac[1] = (macl >> 8) & 0xFF;
	vmxnet3.mac[2] = (macl >> 16) & 0xFF;
	vmxnet3.mac[3] = (macl >> 24) & 0xFF;
	vmxnet3.mac[4] = mach & 0xFF;
	vmxnet3.mac[5] = (mach >> 8) & 0xFF;

	/* low-DMA-window pages: shared page, queue table, rings, buffers */
	if(!(vmxnet3.shared_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	if(!(vmxnet3.qdesc_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	if(!(vmxnet3.tx_ring_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	if(!(vmxnet3.tx_comp_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	if(!(vmxnet3.rx_ring_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	if(!(vmxnet3.rx_comp_phys = vmxnet3_alloc_dma_page())) {
		return NULL;
	}
	for(i = 0; i < TX_RING_ENTRIES; i++) {
		if(!(vmxnet3.tx_buf_phys[i] = vmxnet3_alloc_dma_page())) {
			return NULL;
		}
	}
	for(i = 0; i < RX_RING_ENTRIES; i++) {
		if(!(vmxnet3.rx_buf_phys[i] = vmxnet3_alloc_dma_page())) {
			return NULL;
		}
	}
	memset_b((void *)P2V(vmxnet3.shared_phys), 0, 4096);
	memset_b((void *)P2V(vmxnet3.qdesc_phys), 0, 4096);
	memset_b((void *)P2V(vmxnet3.tx_ring_phys), 0, 4096);
	memset_b((void *)P2V(vmxnet3.tx_comp_phys), 0, 4096);
	memset_b((void *)P2V(vmxnet3.rx_ring_phys), 0, 4096);
	memset_b((void *)P2V(vmxnet3.rx_comp_phys), 0, 4096);

	/* ---- driver shared page ---- */
	vmxnet3_put32(vmxnet3.shared_phys, 0x00, VMXNET3_REV1_MAGIC);
	/* devRead.misc.driverInfo: version, gos, vmxnet3RevSpt, uptVerSpt */
	vmxnet3_put32(vmxnet3.shared_phys, 0x08, 1);	/* version */
	vmxnet3_put32(vmxnet3.shared_phys, 0x0C, 0);	/* gos (Linux, 64-bit) */
	vmxnet3_put32(vmxnet3.shared_phys, 0x10, 1);	/* vmxnet3RevSpt */
	vmxnet3_put32(vmxnet3.shared_phys, 0x14, 1);	/* uptVerSpt */
	vmxnet3_put64(vmxnet3.shared_phys, 0x18, 0);	/* uptFeatures */
	vmxnet3_put64(vmxnet3.shared_phys, 0x20, 0);	/* ddPA */
	vmxnet3_put64(vmxnet3.shared_phys, 0x28, vmxnet3.qdesc_phys);
	vmxnet3_put32(vmxnet3.shared_phys, 0x30, 0);	/* ddLen */
	vmxnet3_put32(vmxnet3.shared_phys, 0x34, 2 * 256);	/* queueDescLen */
	vmxnet3_put32(vmxnet3.shared_phys, 0x38, 1500);	/* mtu */
	vmxnet3_put32(vmxnet3.shared_phys, 0x3C, 1);	/* maxNumRxSG */
	*(volatile unsigned char *)(P2V(vmxnet3.shared_phys) + 0x3E) = 1; /* numTxQueues */
	*(volatile unsigned char *)(P2V(vmxnet3.shared_phys) + 0x3F) = 1; /* numRxQueues */
	/* devRead.intrConf */
	*(volatile unsigned char *)(P2V(vmxnet3.shared_phys) + 0x50) = 0; /* autoMask */
	*(volatile unsigned char *)(P2V(vmxnet3.shared_phys) + 0x51) = 1; /* numIntrs */
	*(volatile unsigned char *)(P2V(vmxnet3.shared_phys) + 0x52) = 0; /* eventIntrIdx */
	vmxnet3_put32(vmxnet3.shared_phys, 0x6C, 0);	/* intrCtrl */
	/* devRead.rxFilterConf */
	vmxnet3_put32(vmxnet3.shared_phys, 0x78, 0x07);	/* rxMode UCAST|MCAST|BCAST */
	vmxnet3_put32(vmxnet3.shared_phys, 0x7C, 0);	/* mfTableLen */
	vmxnet3_put64(vmxnet3.shared_phys, 0x80, 0);	/* mfTablePA */
	/* vfTable at 0x88 stays zeroed (no VLAN filtering) */

	/* ---- queue descriptor table: TX qdesc @0, RX qdesc @256 ---- */
	vmxnet3_put64(vmxnet3.qdesc_phys, 0x10, vmxnet3.tx_ring_phys);
	vmxnet3_put64(vmxnet3.qdesc_phys, 0x20, vmxnet3.tx_comp_phys);
	vmxnet3_put32(vmxnet3.qdesc_phys, 0x38, TX_RING_ENTRIES);
	vmxnet3_put32(vmxnet3.qdesc_phys, 0x40, TX_RING_ENTRIES);
	*(volatile unsigned char *)(P2V(vmxnet3.qdesc_phys) + 0x48) = 0; /* intrIdx */
	vmxnet3_put64(vmxnet3.qdesc_phys, 256 + 0x10, vmxnet3.rx_ring_phys);
	vmxnet3_put64(vmxnet3.qdesc_phys, 256 + 0x20, vmxnet3.rx_comp_phys);
	vmxnet3_put32(vmxnet3.qdesc_phys, 256 + 0x38, RX_RING_ENTRIES);
	vmxnet3_put32(vmxnet3.qdesc_phys, 256 + 0x40, RX_RING_ENTRIES);
	*(volatile unsigned char *)(P2V(vmxnet3.qdesc_phys) + 256 + 0x48) = 0; /* intrIdx */

	/* ---- point the device at the shared page + activate ---- */
	vmxnet3_wl(vmxnet3.mmio1, VD_DSAL, (unsigned int)vmxnet3.shared_phys);
	vmxnet3_wl(vmxnet3.mmio1, VD_DSAH, 0);
	vmxnet3_wl(vmxnet3.mmio1, VD_VRRS, 1);
	vmxnet3_wl(vmxnet3.mmio1, VD_UVRS, 1);
	vmxnet3_wl(vmxnet3.mmio1, VD_CMD, CMD_ACTIVATE_DEV);

	/* ---- fill the RX data ring (all slots, gen = 1) ---- */
	vmxnet3.rx_fill = 0;
	vmxnet3.rx_fill_gen = 1;
	for(i = 0; i < RX_RING_ENTRIES; i++) {
		vmxnet3_rx_fill_desc(i);
	}
	vmxnet3.rx_comp_cur = 0;
	vmxnet3.rx_comp_gen = 1;
	vmxnet3.tx_cur = 0;
	vmxnet3.tx_gen = 1;
	vmxnet3.tx_comp_cur = 0;
	vmxnet3.tx_comp_gen = 1;

	/* ---- IRQ: unmask vector 0, register the handler ---- */
	vmxnet3_wl(vmxnet3.mmio0, PT_IMR, 0);
	{
		static struct interrupt irq_config_vmxnet3 = { 0, "vmxnet3", &vmxnet3_irq_handler, NULL };
		register_irq(vmxnet3.irq, &irq_config_vmxnet3);
	}
	if(vmxnet3.irq < 8) {
		outport_b(0x21, inport_b(0x21) & ~(1 << vmxnet3.irq));
	} else {
		outport_b(0xA1, inport_b(0xA1) & ~(1 << (vmxnet3.irq - 8)));
	}

	vmxnet3.present = 1;
	memcpy_b(vmxnet3_ops.mac, vmxnet3.mac, 6);
	printk("vmxnet3: NIC %x:%x at 0x%lx, IRQ %d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		VMXNET3_VENDOR, pd->device_id, mmio0, vmxnet3.irq,
		vmxnet3.mac[0], vmxnet3.mac[1], vmxnet3.mac[2],
		vmxnet3.mac[3], vmxnet3.mac[4], vmxnet3.mac[5]);
	return &vmxnet3_ops;
}

static struct ext_net_ops vmxnet3_ops = {
	vmxnet3_ext_open,
	vmxnet3_ext_close,
	vmxnet3_ext_bind,
	vmxnet3_ext_listen,
	vmxnet3_ext_connect,
	vmxnet3_ext_accept,
	vmxnet3_ext_ioctl,
	vmxnet3_ext_sendto,
	vmxnet3_ext_recvfrom,
	vmxnet3_ext_read,
	vmxnet3_ext_write,
	vmxnet3_ext_poll,
};
