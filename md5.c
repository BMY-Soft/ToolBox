#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/md5.h>

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

int c = 0;

static int usage() {
	fprintf(stderr,"md5"
#if defined _WIN32 && !defined _WIN32_WNT_NATIVE
			".exe"
#endif
			" [-c] <file> [...]\n");
	return -1;
}

static int do_md5(const char *path) {
	unsigned int i;
	int fd;
	MD5_CTX md5_ctx;
	unsigned char md5[MD5_DIGEST_LENGTH];

	fd = open(path, O_RDONLY);
	if(fd == -1) {
		fprintf(stderr,"could not open %s, %s\n", path, strerror(errno));
		return -1;
	}

	/* Note that bionic's MD5_* functions return void. */
	MD5_Init(&md5_ctx);

	while(1) {
		char buf[4096];
		ssize_t rlen;
		rlen = read(fd, buf, sizeof(buf));
		if(rlen == 0) break;
		if(rlen < 0) {
			close(fd);
			fprintf(stderr,"could not read %s, %s\n", path, strerror(errno));
			return -1;
		}
		MD5_Update(&md5_ctx, buf, rlen);
	}
	if(close(fd) < 0) {
		fprintf(stderr,"could not close %s, %s\n", path, strerror(errno));
		return -1;
	}

	MD5_Final(md5, &md5_ctx);

	for(i = 0; i < (int)sizeof(md5); i++) printf("%02x", md5[i]);
	if(c) putchar('\n'); else printf("  %s\n", path);

	return 0;
}

int main(int argc, char *argv[]) {
	int i, ret = 0;

	if(argc < 2) return usage();

	if(strcmp(argv[1], "-c") == 0) {
		if (argc < 3) return usage();
		c = 1;
		argv[1] = argv[0];
		argc--;
		argv++;
	}

	/* loop over the file args */
	for (i = 1; i < argc; i++) {
		if(do_md5(argv[i]) < 0) ret = 1;
	}

	return ret;
}
