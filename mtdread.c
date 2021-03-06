#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "mtd.h"
//#include <mtd/mtd-user.h>

#include <sys/ioctl.h>
#ifdef __sun
#include <sys/ioccom.h>
#endif

#if defined __APPLE__ || defined __FreeBSD__ || defined __INTERIX
static loff_t lseek64(int fd, loff_t offset, int whence) {
	if(whence == SEEK_SET || whence == SEEK_END) {
		if(lseek(fd, 0, whence) < 0) return -1;
	}
	while(offset > INT32_MAX) {
		if(lseek(fd, INT32_MAX, SEEK_CUR) < 0) return -1;
		//r += INT32_MAX;
		offset -= INT32_MAX;
	}
	return lseek(fd, offset, SEEK_CUR);
}
#endif

static int test_empty(const char *buf, size_t size) {
	while(size--) {
		if((unsigned char)*buf++ != 0xff) return 0;
	}
	return 1;
}

int main(int argc, char **argv) {
	char *devname = NULL;
	char *filename = NULL;
	char *statusfilename = NULL;
	char *statusext = ".stat";
	int fd;
	int outfd = -1;
	FILE *statusfile = NULL;
	int ret;
	int verbose = 0;
	void *buffer;
	loff_t pos, opos, end, bpos;
	loff_t start = 0, len = 0;
	int i;
	int empty_pages = 0;
	int page_count = 0;
	int bad_block;
	int rawmode = 0;
	uint32_t *oob_data;
	uint8_t *oob_fixed;
	size_t spare_size = 64;
	struct mtd_info_user mtdinfo;
	struct mtd_ecc_stats initial_ecc, last_ecc, ecc;
	struct mtd_oob_buf oobbuf;
	//struct nand_ecclayout ecclayout;
	nand_ecclayout_t ecclayout;

	while(1) {
		int c = getopt(argc, argv, "d:f:s:S:L:Rhv");
		if(c == -1) break;
		switch (c) {
			case 'd':
				devname = optarg;
				break;
			case 'f':
				filename = optarg;
				break;
			case 's':
				spare_size = atoi(optarg);
				break;
			case 'S':
				start = strtoll(optarg, NULL, 0);
				break;
			case 'L':
				len = strtoll(optarg, NULL, 0);
				break;
			case 'R':
				rawmode = 1;
				break;
			case 'v':
				verbose++;
				break;
			case 'h':
				fprintf(stderr, "%s [-d <dev>] [-f <file>] [-s <sparesize>] [-vh]\n\n"
						"	-d <dev>   Read from <dev>\n"
						"	-f <file>  Write to <file>\n"
						"	-s <size>  Number of spare bytes in file (default 64)\n"
						"	-R         Raw mode\n"
						"	-S <start> Start offset (default 0)\n"
						"	-L <len>   Length (default 0)\n"
						"	-v         Print info\n"
						"	-h         Print help\n", argv[0]);
				return -1;
			case '?':
				//fprintf(stderr, "%s: invalid option -%c\n", argv[0], optopt);
				return 1;
		}
	}

	if(argc < 2) {
		fprintf(stderr, "%s: missing arguments\n", argv[0]);
		return 1;
	}
	if(optind < argc) {
		fprintf(stderr, "%s: extra arguments\n", argv[0]);
		return 1;
	}
	if(!devname) {
		fprintf(stderr, "%s: specify device name\n", argv[0]);
		return 1;
	}

	fd = open(devname, O_RDONLY);
	if(fd == -1) {
		fprintf(stderr, "cannot open %s, %s\n", devname, strerror(errno));
		return 1;
	}

	if(filename) {
		outfd = creat(filename, 0666);
		if(outfd == -1) {
			fprintf(stderr, "cannot open %s, %s\n", filename, strerror(errno));
			return 1;
		}
		statusfilename = malloc(strlen(filename) + strlen(statusext) + 1);
		strcpy(statusfilename, filename);
		strcat(statusfilename, statusext);
		statusfile = fopen(statusfilename, "w+");
		if(!statusfile) {
			fprintf(stderr, "cannot open %s, %s\n", statusfilename, strerror(errno));
			return 1;
		}
	}

	ret = ioctl(fd, MEMGETINFO, &mtdinfo);
	if(ret < 0) {
		fprintf(stderr, "failed get mtd info for %s, %s\n", devname, strerror(errno));
		return 1;
	}

	if(verbose) {
		printf("size: %u\n", mtdinfo.size);
		printf("erase size: %u\n", mtdinfo.erasesize);
		printf("write size: %u\n", mtdinfo.writesize);
		printf("oob size: %u\n", mtdinfo.oobsize);
	}

	buffer = malloc(mtdinfo.writesize + mtdinfo.oobsize + spare_size);
	if(!buffer) {
		fprintf(stderr, "failed allocate readbuffer size %u\n",
				mtdinfo.writesize + mtdinfo.oobsize);
		return 1;
	}

	oobbuf.length = mtdinfo.oobsize;
	oob_data = (uint32_t *)((uint8_t *)buffer + mtdinfo.writesize);
	memset(oob_data, 0xff, mtdinfo.oobsize + spare_size);
	oobbuf.ptr = (uint8_t *)oob_data + spare_size;

	ret = ioctl(fd, ECCGETLAYOUT, &ecclayout);
	if(ret < 0) {
		fprintf(stderr, "failed get ecc layout for %s, %s\n", devname, strerror(errno));
		return 1;
	}
	if(verbose) {
		printf("ecc bytes: %u\n", ecclayout.eccbytes);
		printf("oobavail: %u\n", ecclayout.oobavail);
	}
	if(ecclayout.oobavail > spare_size) {
		printf("oobavail, %d > image spare size, %ld\n", ecclayout.oobavail, (long int)spare_size);
	}

	ret = ioctl(fd, ECCGETSTATS, &initial_ecc);
	if(ret < 0) {
		fprintf(stderr, "failed get ecc stats for %s, %s\n", devname, strerror(errno));
		return 1;
	}
	last_ecc = initial_ecc;

	if(verbose) {
		printf("initial ecc corrected: %u\n", initial_ecc.corrected);
		printf("initial ecc failed: %u\n", initial_ecc.failed);
		printf("initial ecc badblocks: %u\n", initial_ecc.badblocks);
		printf("initial ecc bbtblocks: %u\n", initial_ecc.bbtblocks);
	}

	if(rawmode) {
		rawmode = mtdinfo.oobsize;
		ret = ioctl(fd, MTDFILEMODE, MTD_MODE_RAW);
		if(ret < 0) {
			fprintf(stderr, "failed set raw mode for %s, %s\n", devname, strerror(errno));
			return 1;
		}
	}

	end = len ? (start + len) : mtdinfo.size;
	for(pos = start, opos = 0; pos < end; pos += mtdinfo.writesize) {
		bad_block = 0;
		if(verbose > 3) printf("reading at %llx\n", (long long int)pos);
		lseek64(fd, pos, SEEK_SET);
		ret = read(fd, buffer, mtdinfo.writesize + rawmode);
		if(ret < (int)mtdinfo.writesize) {
			fprintf(stderr, "short read at %llx, %d\n", (long long int)pos, ret);
			bad_block = 2;
		}
		if(!rawmode) {
			oobbuf.start = pos;
			ret = ioctl(fd, MEMREADOOB, &oobbuf);
			if(ret < 0) {
				fprintf(stderr, "failed to read oob data at %llx, %d\n", (long long int)pos, ret);
				bad_block = 2;
			}
		}
		ret = ioctl(fd, ECCGETSTATS, &ecc);
		if(ret < 0) {
			fprintf(stderr, "failed get ecc stats for %s, %s\n", devname, strerror(errno));
			return 1;
		}
		bpos = pos / mtdinfo.erasesize * mtdinfo.erasesize;
		ret = ioctl(fd, MEMGETBADBLOCK, &bpos);
		if(ret < 0 && errno != EOPNOTSUPP) {
			printf("badblock at %llx\n", (long long int)pos);
			bad_block = 1;
		}
		if(ecc.corrected != last_ecc.corrected) {
			printf("ecc corrected, %u, at %llx\n", ecc.corrected - last_ecc.corrected, (long long int)pos);
		}
		if(ecc.failed != last_ecc.failed) {
			printf("ecc failed, %u, at %llx\n", ecc.failed - last_ecc.failed, (long long int)pos);
		}
		if(ecc.badblocks != last_ecc.badblocks) {
			printf("ecc badblocks, %u, at %llx\n", ecc.badblocks - last_ecc.badblocks, (long long int)pos);
		}
		if(ecc.bbtblocks != last_ecc.bbtblocks) {
			printf("ecc bbtblocks, %u, at %llx\n", ecc.bbtblocks - last_ecc.bbtblocks, (long long int)pos);
		}

		if(!rawmode) {
			oob_fixed = (uint8_t *)oob_data;
			for(i = 0; i < MTD_MAX_OOBFREE_ENTRIES; i++) {
				int len = ecclayout.oobfree[i].length;
				if(oob_fixed + len > oobbuf.ptr) len = oobbuf.ptr - oob_fixed;
				if(len) {
					memcpy(oob_fixed, oobbuf.ptr + ecclayout.oobfree[i].offset, len);
					oob_fixed += len;
				}
			}
		}

		if(outfd >= 0) {
			ret = write(outfd, buffer, mtdinfo.writesize + spare_size);
			if(ret < (int)(mtdinfo.writesize + spare_size)) {
				fprintf(stderr, "short write at %llx, %d\n", (long long int)pos, ret);
				close(outfd);
				outfd = -1;
			}
			if(ecc.corrected != last_ecc.corrected) {
				fprintf(statusfile, "%08llx: ecc corrected\n", (long long int)opos);
			}
			if(ecc.failed != last_ecc.failed) {
				fprintf(statusfile, "%08llx: ecc failed\n", (long long int)opos);
			}
			if(bad_block == 1) {
				fprintf(statusfile, "%08llx: badblock\n", (long long int)opos);
			} else if (bad_block == 2) {
				fprintf(statusfile, "%08llx: read error\n", (long long int)opos);
			}
			opos += mtdinfo.writesize + spare_size;
		}

		last_ecc = ecc;
		page_count++;
		if(test_empty(buffer, mtdinfo.writesize + mtdinfo.oobsize + spare_size)) {
			empty_pages++;
		} else if(verbose > 2 || (verbose > 1 && !(pos & (mtdinfo.erasesize - 1)))) {
			printf("page at %llx (%d oobbytes): %08x %08x %08x %08x "
				"%08x %08x %08x %08x\n", (long long int)pos, oobbuf.start,
				oob_data[0], oob_data[1], oob_data[2], oob_data[3],
				oob_data[4], oob_data[5], oob_data[6], oob_data[7]);
		}
	}

	if(outfd >= 0) {
		fprintf(statusfile, "read %d pages, %d empty\n", page_count, empty_pages);
		fprintf(statusfile, "total ecc corrected, %u\n", ecc.corrected - initial_ecc.corrected);
		fprintf(statusfile, "total ecc failed, %u\n", ecc.failed - initial_ecc.failed);
		fprintf(statusfile, "total ecc badblocks, %u\n", ecc.badblocks - initial_ecc.badblocks);
		fprintf(statusfile, "total ecc bbtblocks, %u\n", ecc.bbtblocks - initial_ecc.bbtblocks);
	}
	if(verbose) {
		printf("total ecc corrected, %u\n", ecc.corrected - initial_ecc.corrected);
		printf("total ecc failed, %u\n", ecc.failed - initial_ecc.failed);
		printf("total ecc badblocks, %u\n", ecc.badblocks - initial_ecc.badblocks);
		printf("total ecc bbtblocks, %u\n", ecc.bbtblocks - initial_ecc.bbtblocks);
	}
	printf("read %d pages, %d empty\n", page_count, empty_pages);

	return 0;
}
