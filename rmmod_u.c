/*	rmmod - toolbox
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <asm/unistd.h>

extern int delete_module(const char *, unsigned int);

int rmmod_main(int argc, char **argv) {
	int ret, i;
	char *modname, *dot;

	/* make sure we've got an argument */
	if(argc < 2) {
		fprintf(stderr, "Usage: rmmod <module>\n");
		return -1;
	}

	/* if given /foo/bar/blah.ko, make a weak attempt
	 * to convert to "blah", just for convenience
	 */
	modname = strrchr(argv[1], '/');
	if(!modname) modname = argv[1];
	else modname++;

	dot = strchr(argv[1], '.');
	if(dot) *dot = 0;

	/* Replace "-" with "_". This would keep rmmod
	 * compatible with module-init-tools version of
	 * rmmod
	 */
	for (i = 0; modname[i] != '\0'; i++) {
		if (modname[i] == '-') modname[i] = '_';
	}

	/* pass it to the kernel */
	ret = delete_module(modname, O_NONBLOCK | O_EXCL);
	if (ret != 0) {
		fprintf(stderr, "rmmod: delete_module '%s' failed (errno %d)\n",
			modname, errno);
		return -1;
	}

	return 0;
}
