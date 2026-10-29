#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "sws.h"

#if !defined(__GLIBC__) && !defined(__linux__)
char *strptime(const char *s, const char *format, struct tm *tm);
time_t timegm(struct tm *tm);
#endif

static int
read_line(int fd, char *buf, size_t maxlen)
{
	size_t i = 0;
	char c;
	ssize_t n;
	int truncated = 0;

	while (1) {
		n = read(fd, &c, 1);
		if (n <= 0) {
			if (i == 0)
				return -1;
			break;
		}
		if (c == '\r')
			continue;
		if (c == '\n')
			break;
		if (i + 1 < maxlen) {
			buf[i++] = c;
		} else {
			truncated = 1;
		}
	}
	buf[i] = '\0';
	if (truncated)
		return -2;
	return (int)i;
}

static void
drain_headers(int fd)
{
	char buf[MAX_LINE];
	for (int i = 0; i < 100; i++) {
		int len = read_line(fd, buf, sizeof(buf));
		if (len <= 0)
			break;
	}
}

static int
hex_val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int
urldecode(char *dst, const char *src, size_t dst_size)
{
	size_t i = 0;

	while (*src) {
		if (i + 1 >= dst_size)
			return -1;
		if (*src == '%') {
			if (src[1] == '\0' || src[2] == '\0')
				return -1;
			int h1 = hex_val(src[1]);
			int h2 = hex_val(src[2]);
			if (h1 == -1 || h2 == -1)
				return -1;
			char decoded_char = (char)((h1 << 4) | h2);
			if (decoded_char == '\0')
				return -1;
			dst[i++] = decoded_char;
			src += 3;
		} else {
			dst[i++] = *src++;
		}
	}
	dst[i] = '\0';
	return 0;
}

static int
normalize_path(char *dst, size_t dst_size, const char *src)
{
	char tmp[PATH_MAX];
	char *segs[PATH_MAX / 2];
	int nsegs = 0;
	size_t len = strlen(src);
	int trailing_slash = 0;

	if (src[0] != '/')
		return 400;

	if (len >= sizeof(tmp))
		return 400;

	if (len > 1 && src[len - 1] == '/')
		trailing_slash = 1;

	memcpy(tmp, src, len + 1);

	char *p = tmp;
	while (*p) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		char *start = p;
		while (*p && *p != '/')
			p++;
		if (*p)
			*p++ = '\0';

		if (strcmp(start, ".") == 0) {
			continue;
		} else if (strcmp(start, "..") == 0) {
			if (nsegs == 0)
				return 403;
			nsegs--;
		} else {
			if ((size_t)nsegs >= sizeof(segs) / sizeof(segs[0]))
				return 400;
			segs[nsegs++] = start;
		}
	}

	if (nsegs == 0) {
		if (dst_size < 2)
			return 400;
		dst[0] = '/';
		dst[1] = '\0';
		return 0;
	}

	size_t cur = 0;
	for (int i = 0; i < nsegs; i++) {
		size_t seg_len = strlen(segs[i]);
		if (cur + 1 + seg_len >= dst_size)
			return 400;
		dst[cur++] = '/';
		memcpy(dst + cur, segs[i], seg_len);
		cur += seg_len;
	}
	if (trailing_slash) {
		if (cur + 1 >= dst_size)
			return 400;
		dst[cur++] = '/';
	}
	dst[cur] = '\0';
	return 0;
}

int
parse_request(int fd, struct request *r)
{
	char line[MAX_LINE];
	int len = read_line(fd, line, sizeof(line));
	if (len <= 0)
		return 400;

	size_t line_len = (size_t)len;
	if (line_len >= sizeof(r->first_line))
		return 400;
	memcpy(r->first_line, line, line_len + 1);

	char *p = line;
	while (*p == ' ')
		p++;
	if (!*p)
		return 400;

	char *m_start = p;
	while (*p && *p != ' ')
		p++;
	if (!*p)
		return 400;
	*p++ = '\0';

	while (*p == ' ')
		p++;
	if (!*p)
		return 400;
	char *u_start = p;
	while (*p && *p != ' ')
		p++;
	if (!*p)
		return 400;
	*p++ = '\0';

	while (*p == ' ')
		p++;
	if (!*p)
		return 400;
	char *v_start = p;
	while (*p && *p != ' ')
		p++;
	if (*p) {
		*p++ = '\0';
		while (*p == ' ')
			p++;
		if (*p != '\0') {
			drain_headers(fd);
			return 400;
		}
	}

	size_t m_len = strlen(m_start);
	if (m_len >= sizeof(r->method)) {
		drain_headers(fd);
		return 501;
	}
	memcpy(r->method, m_start, m_len + 1);

	if (strcmp(r->method, "GET") == 0) {
		r->is_head = 0;
	} else if (strcmp(r->method, "HEAD") == 0) {
		r->is_head = 1;
	} else {
		drain_headers(fd);
		return 501;
	}

	if (strncmp(v_start, "HTTP/", 5) != 0) {
		drain_headers(fd);
		return 400;
	}
	if (strcmp(v_start, "HTTP/1.0") != 0 && strcmp(v_start, "HTTP/1.1") != 0) {
		drain_headers(fd);
		return 505;
	}

	size_t u_len = strlen(u_start);
	if (u_len >= sizeof(r->uri)) {
		drain_headers(fd);
		return 400;
	}
	memcpy(r->uri, u_start, u_len + 1);

	char *raw_path = u_start;
	if (strncasecmp(raw_path, "http://", 7) == 0) {
		char *slash = strchr(raw_path + 7, '/');
		if (slash != NULL)
			raw_path = slash;
		else
			raw_path = "/";
	}

	char *q = strchr(raw_path, '?');
	if (q != NULL) {
		*q = '\0';
		q++;
		size_t q_len = strlen(q);
		if (q_len >= sizeof(r->query)) {
			drain_headers(fd);
			return 400;
		}
		memcpy(r->query, q, q_len + 1);
	} else {
		r->query[0] = '\0';
	}

	char decoded[PATH_MAX];
	if (urldecode(decoded, raw_path, sizeof(decoded)) != 0) {
		drain_headers(fd);
		return 400;
	}

	int norm_err = normalize_path(r->path, sizeof(r->path), decoded);
	if (norm_err != 0) {
		drain_headers(fd);
		return norm_err;
	}

	while (1) {
		len = read_line(fd, line, sizeof(line));
		if (len < 0)
			return 400;
		if (len == 0)
			break;

		if (strncasecmp(line, "If-Modified-Since:", 18) == 0) {
			char *val = line + 18;
			while (*val == ' ' || *val == '\t')
				val++;
			struct tm tm;
			memset(&tm, 0, sizeof(tm));
			if (strptime(val, "%a, %d %b %Y %H:%M:%S GMT", &tm) != NULL ||
			    strptime(val, "%a, %d %b %Y %T GMT", &tm) != NULL ||
			    strptime(val, "%a, %d-%b-%y %T GMT", &tm) != NULL ||
			    strptime(val, "%a %b %d %T %Y", &tm) != NULL) {
				r->ims = timegm(&tm);
			}
		}
	}

	return 0;
}
