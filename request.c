#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sws.h"

int
parse_request(int fd, struct request *r)
{
	/* TODO: doc request line + header, check method/version,
	 * decode uri, chan ../, If-Modified-Since */
	(void)fd;
	(void)r;
	return 501;
}
