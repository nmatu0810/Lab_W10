#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sws.h"

const char *
status_text(int status)
{
	switch (status) {
	case 200: return "OK";
	case 301: return "Moved Permanently";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 505: return "HTTP Version Not Supported";
	default:  return "Unknown";
	}
}

void
send_headers(int fd, struct request *r, int status, const char *ctype,
             off_t len, time_t mtime)
{
	/* TODO: status line, Date, Server, Last-Modified, Content-Type,
	 * Content-Length */
	(void)fd;
	(void)ctype;
	(void)len;
	(void)mtime;
	r->status = status;
}

void
send_error(int fd, struct request *r, int status)
{
	/* TODO: trang html bao loi */
	send_headers(fd, r, status, "text/html", 0, 0);
}
