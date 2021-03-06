/*	rotatefb - toolbox
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

//#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

int rotatefb_main(int argc, char *argv[]) {
	char *fbdev = "/dev/fb0";
	int rotation = 0;
	int fd;
	int res;
	struct fb_var_screeninfo fbinfo;

	while(1) {
		int c = getopt(argc, argv, "d:");
		if(c == EOF) break;
		switch (c) {
			case 'd':
				fbdev = optarg;
				break;
			case '?':
				//fprintf(stderr, "%s: invalid option -%c\n", argv[0], optopt);
				return 1;
		}
	}

	if(optind + 1 != argc) {
		fprintf(stderr, "%s: Rotation not specified\n", argv[0]);
		return 1;
	}
	rotation = atoi(argv[optind]);

	fd = open(fbdev, O_RDWR);
	if(fd == -1) {
		fprintf(stderr, "cannot open %s, %s\n", fbdev, strerror(errno));
		return 1;
	}

	res = ioctl(fd, FBIOGET_VSCREENINFO, &fbinfo);
	if(res < 0) {
		fprintf(stderr, "failed to get fbinfo: %s\n", strerror(errno));
		return 1;
	}
	if((fbinfo.rotate ^ rotation) & 1) {
		unsigned int xres = fbinfo.yres;
		fbinfo.yres = fbinfo.xres;
		fbinfo.xres = xres;
		fbinfo.xres_virtual = fbinfo.xres;
		fbinfo.yres_virtual = fbinfo.yres * 2;
		if(fbinfo.yoffset == xres) fbinfo.yoffset = fbinfo.yres;
	}
	fbinfo.rotate = rotation; 
	res = ioctl(fd, FBIOPUT_VSCREENINFO, &fbinfo);
	if(res < 0) {
		fprintf(stderr, "failed to set fbinfo: %s\n", strerror(errno));
		return 1;
	}

	return 0;
}
