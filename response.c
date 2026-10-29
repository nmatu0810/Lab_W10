#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sws.h"

static ssize_t
write_all(int fd, const void *buf, size_t count)
{
	size_t total = 0;
	const char *p = (const char *)buf;

	while (total < count) {
		ssize_t n = write(fd, p + total, count - total);
		if (n <= 0)
			break;
		total += (size_t)n;
	}
	return (ssize_t)total;
}

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
	char header[MAX_LINE];
	int off = 0;
	time_t now = time(NULL);
	struct tm tm_now;
	struct tm *gm;
	char date_str[64];

	r->status = status;

	gm = gmtime(&now);
	if (gm != NULL)
		tm_now = *gm;
	else
		memset(&tm_now, 0, sizeof(tm_now));
	strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_now);

	off += snprintf(header + off, sizeof(header) - off,
	    "HTTP/1.0 %d %s\r\n", status, status_text(status));
	off += snprintf(header + off, sizeof(header) - off,
	    "Date: %s\r\n", date_str);
	off += snprintf(header + off, sizeof(header) - off,
	    "Server: %s\r\n", SERVER_NAME);

	if (mtime > 0) {
		struct tm tm_mod;
		char mtime_str[64];
		gm = gmtime(&mtime);
		if (gm != NULL)
			tm_mod = *gm;
		else
			memset(&tm_mod, 0, sizeof(tm_mod));
		strftime(mtime_str, sizeof(mtime_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_mod);
		off += snprintf(header + off, sizeof(header) - off,
		    "Last-Modified: %s\r\n", mtime_str);
	}

	if (ctype != NULL && *ctype != '\0') {
		off += snprintf(header + off, sizeof(header) - off,
		    "Content-Type: %s\r\n", ctype);
	}

	if (status != 304 && len >= 0) {
		off += snprintf(header + off, sizeof(header) - off,
		    "Content-Length: %lld\r\n", (long long)len);
	}

	off += snprintf(header + off, sizeof(header) - off,
	    "Connection: close\r\n\r\n");

	/* sent chi dem byte body */
	if (off > 0)
		write_all(fd, header, (size_t)off);
}

void
send_error(int fd, struct request *r, int status)
{
	char body[2048];
	const char *text = status_text(status);
	int body_len = snprintf(body, sizeof(body),
	    "<!DOCTYPE html>\r\n"
	    "<html>\r\n"
	    "<head><title>%d %s</title></head>\r\n"
	    "<body>\r\n"
	    "<h1>%d %s</h1>\r\n"
	    "<hr>\r\n"
	    "<address>%s</address>\r\n"
	    "</body>\r\n"
	    "</html>\r\n",
	    status, text, status, text, SERVER_NAME);

	if (body_len < 0)
		body_len = 0;

	send_headers(fd, r, status, "text/html", (off_t)body_len, 0);

	if (!r->is_head && body_len > 0) {
		ssize_t n = write_all(fd, body, (size_t)body_len);
		if (n > 0)
			r->sent += n;
	}
}
