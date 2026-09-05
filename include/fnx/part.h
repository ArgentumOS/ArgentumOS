/*
 * fnx/include/fnx/part.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_PART_H
#define _FNX_PART_H

#define PARTITION_BLOCK		0
#define NR_PARTITIONS		4	/* partitions in the MBR */
#define MBR_CODE_SIZE		446
#define ACTIVE_PART		0x80
#define MAX_PARTITIONS		128	/* scan output cap (GPT/max minors) */

struct hd_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short int cylinders;
	unsigned int start;
};

struct partition {
	unsigned char status;
	unsigned char head;
	unsigned char sector;
	unsigned char cyl;
	unsigned char type;
	unsigned char endhead;
	unsigned char endsector;
	unsigned char endcyl;
	unsigned int startsect;
	unsigned int nr_sects;
};

int read_msdos_partition(__dev_t, struct partition *);

/* shared GPT + MBR(+EBR) parser (drivers/block/partition.c). Pure: reads
 * 512-byte sectors through the supplied callback. out[] is indexed by
 * partition number (out[n-1] = partition n); returns the highest
 * partition number found, or 0 for an unpartitioned/unreadable disk. */
typedef int (*part_sector_read_t)(void *, unsigned long long,
				  unsigned char *);
int partition_parse(void *ctx, part_sector_read_t rd, struct partition *out,
		    int max);

/* kernel convenience: parse the table on a whole-disk block device
 * (drivers/block/part.c) */
int read_partitions(__dev_t, struct partition *, int);

#endif /* _FNX_PART_H */
