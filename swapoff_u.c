/*	swapoff - toolbox
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include <stdio.h>
#include <unistd.h>
#if defined __linux__ || defined __gnu_hurd__ || defined __SVR4
#include <sys/swap.h>
#endif
#include <string.h>
#include <errno.h>

#ifdef __SVR4
static int swapoff(char *path) {
	struct swapres res = { .sr_name = path };
	return swapctl(SC_REMOVE, &res);
}
#endif

int swapoff_main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <path>\n", argv[0]);
		return -EINVAL;
	}

	if(swapoff(argv[1]) < 0) {
		int e = errno;
		fprintf(stderr, "swapoff failed for %s, %s\n", argv[1], strerror(e));
		return -e;
	}

	return 0;
}
