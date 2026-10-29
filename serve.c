#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sws.h"

const char *
mime_type(const char *path)
{
	/* TODO */
	(void)path;
	return "application/octet-stream";
}

void
serve_path(int fd, struct request *r)
{
	/* TODO: file thuong, 304, thu muc (index.html / liet ke), ~user */
	send_error(fd, r, 404);
}
