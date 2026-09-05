/* M0 host test for the pure partition parsers (drivers/block/partition.c).
 * Loads fixture disk images, parses them, prints the result in the same
 * format as the *.expect files for diffing. Compiles standalone:
 *   gcc -I<repo>/include -o parttest main.c <repo>/drivers/block/partition.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fnx/part.h>

struct memdisk {
	unsigned char *data;
	unsigned long nbytes;
};

static int rd_sector(void *ctx, unsigned long long lba, unsigned char *out)
{
	struct memdisk *m = (struct memdisk *)ctx;

	if(lba * 512ull + 512ull > m->nbytes) {
		return -1;
	}
	memcpy(out, m->data + lba * 512ull, 512);
	return 0;
}

int main(int argc, char **argv)
{
	int fi;

	for(fi = 1; fi < argc; fi++) {
		FILE *f;
		long size;
		struct memdisk md;
		struct partition out[MAX_PARTITIONS];
		int n, i;

		f = fopen(argv[fi], "rb");
		if(!f) {
			fprintf(stderr, "cannot open %s\n", argv[fi]);
			return 1;
		}
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		md.data = (unsigned char *)malloc(size);
		md.nbytes = size;
		if(fread(md.data, 1, size, f) != (size_t)size) {
			return 1;
		}
		fclose(f);

		n = partition_parse(&md, rd_sector, out, MAX_PARTITIONS);
		printf("highest %d\n", n);
		for(i = 0; i < n; i++) {
			if(out[i].type) {
				printf("p%d type=0x%02x start=%u len=%u\n",
				       i + 1, out[i].type, out[i].startsect,
				       out[i].nr_sects);
			}
		}
		free(md.data);
	}
	return 0;
}
