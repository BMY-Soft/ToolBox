/*
 * This file has no copyright assigned and is placed in the Public Domain.
 * No warranty is given.
 *
 * Written by PC GO
 *
 */

#include <windows.h>
#include <unistd.h>
#include <errno.h>

off_t lseek(int fd, off_t offset, int whence) {
	if(whence < 0 || whence > 2) {
		errno = EINVAL;
		return -1;
	}
	return SetFilePointer((void *)fd, offset, NULL, whence);
}
