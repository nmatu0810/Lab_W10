#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "sws.h"

/* Contract: r->uri contains the decoded URL path (optional query ignored).
 * r->ims == 0 means absent/invalid; is_head is set by parse_request.
 * This module resolves the filesystem path and counts BODY bytes in sent.
 * send_headers must not count body bytes or send a body for HEAD/304.
 */
const char *
mime_type(const char *path)
{
	static const struct { const char *ext; const char *type; } types[] = {
		{ ".html", "text/html" }, { ".htm", "text/html" },
		{ ".txt", "text/plain" }, { ".css", "text/css" },
		{ ".js", "application/javascript" },
		{ ".json", "application/json" }, { ".pdf", "application/pdf" },
		{ ".png", "image/png" }, { ".jpg", "image/jpeg" },
		{ ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
		{ ".svg", "image/svg+xml" }, { ".ico", "image/x-icon" },
		{ ".webp", "image/webp" }, { ".zip", "application/zip" }
	};
	const char *ext = strrchr(path, '.');
	size_t i;

	if (ext != NULL)
		for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
			if (strcasecmp(ext, types[i].ext) == 0)
				return types[i].type;
	return "application/octet-stream";
}

static int
file_error(void)
{
	if (errno == ENOENT || errno == ENOTDIR)
		return 404;
	if (errno == EACCES || errno == EPERM || errno == ELOOP)
		return 403;
	return 500;
}

static int
inside_root(const char *root, const char *path)
{
	size_t n = strlen(root);

	return strcmp(root, "/") == 0 ||
	    (strncmp(root, path, n) == 0 &&
	    (path[n] == '\0' || path[n] == '/'));
}

static int
join_path(char *out, size_t cap, const char *base, const char *tail)
{
	int n = snprintf(out, cap, "%s/%s", base, tail);

	return n >= 0 && (size_t)n < cap;
}

/* Resolve home using the account database, not a hard-coded /home path. */
static int
resolve_path(struct request *r, char *root, char *url)
{
	char base[PATH_MAX], candidate[PATH_MAX];
	const char *tail;
	char *slash;
	struct passwd *pw;
	size_t n = strcspn(r->uri, "?");

	if (n == 0 || n >= PATH_MAX || r->uri[0] != '/')
		return 400;
	memcpy(url, r->uri, n);
	url[n] = '\0';
	/* Reject parent components even if they would stay inside the root. */
	for (tail = url; *tail != '\0'; ) {
		size_t len;
		while (*tail == '/')
			tail++;
		len = strcspn(tail, "/");
		if (len == 2 && strncmp(tail, "..", 2) == 0)
			return 403;
		tail += len;
	}
	tail = url + 1;
	if (url[1] == '~') {
		char user[PATH_MAX];
		slash = strchr(url + 2, '/');
		n = slash != NULL ? (size_t)(slash - (url + 2)) : strlen(url + 2);
		if (n == 0 || n >= sizeof(user))
			return 404;
		memcpy(user, url + 2, n);
		user[n] = '\0';
		errno = 0;
		pw = getpwnam(user);
		if (pw == NULL)
			return errno == 0 ? 404 : 500;
		if (!join_path(base, sizeof(base), pw->pw_dir, "sws"))
			return 400;
		tail = slash != NULL ? slash + 1 : "";
	} else {
		if (cfg.docroot == NULL || strlen(cfg.docroot) >= sizeof(base))
			return 500;
		strcpy(base, cfg.docroot);
	}
	if (realpath(base, root) == NULL)
		return file_error();
	if (!join_path(candidate, sizeof(candidate), root, tail))
		return 400;
	if (realpath(candidate, r->path) == NULL)
		return file_error();
	return inside_root(root, r->path) ? 0 : 403;
}

static int
write_body(int fd, struct request *r, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		r->sent += n;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static void
serve_file(int fd, struct request *r, const char *path)
{
	struct stat st;
	char buf[16384];
	off_t left;
	int in = open(path, O_RDONLY | O_NONBLOCK);

	if (in < 0) {
		send_error(fd, r, file_error());
		return;
	}
	if (fstat(in, &st) < 0) {
		int status = file_error();
		close(in);
		send_error(fd, r, status);
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		close(in);
		send_error(fd, r, 403);
		return;
	}
	if (r->ims != 0 && st.st_mtime <= r->ims) {
		send_headers(fd, r, 304, mime_type(path), st.st_size, st.st_mtime);
		close(in);
		return;
	}
	send_headers(fd, r, 200, mime_type(path), st.st_size, st.st_mtime);
	left = st.st_size;
	while (!r->is_head && left > 0) {
		size_t want = left > (off_t)sizeof(buf) ? sizeof(buf) : (size_t)left;
		ssize_t n = read(in, buf, want);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0 || write_body(fd, r, buf, (size_t)n) < 0)
			break;
		left -= n;
	}
	close(in);
}

static void
html_text(FILE *out, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': fputs("&amp;", out); break;
		case '<': fputs("&lt;", out); break;
		case '>': fputs("&gt;", out); break;
		case '"': fputs("&quot;", out); break;
		case '\'': fputs("&#39;", out); break;
		default: fputc((unsigned char)*s, out); break;
		}
	}
}

static void
url_text(FILE *out, const char *s, int keep_slash)
{
	const unsigned char *p = (const unsigned char *)s;

	for (; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') || strchr("-._~", *p) != NULL ||
		    (keep_slash && *p == '/'))
			fputc(*p, out);
		else
			fprintf(out, "%%%02X", *p);
	}
}

static int
visible(const struct dirent *d)
{
	return d->d_name[0] != '.';
}

static void
serve_directory(int fd, struct request *r, const char *url)
{
	struct dirent **list;
	FILE *out;
	char buf[16384];
	off_t len;
	size_t n;
	int count, i;

	/* sap xep theo ten, bo file an */
	if ((count = scandir(r->path, &list, visible, alphasort)) < 0) {
		send_error(fd, r, file_error());
		return;
	}
	out = tmpfile();
	if (out == NULL) {
		for (i = 0; i < count; i++)
			free(list[i]);
		free(list);
		send_error(fd, r, 500);
		return;
	}
	fputs("<!doctype html><html><head><meta charset=\"utf-8\">"
	    "<title>Directory listing</title></head><body><h1>Index of ", out);
	html_text(out, url);
	fputs("</h1><ul>\n", out);
	for (i = 0; i < count; i++) {
		fputs("<li><a href=\"", out);
		url_text(out, url, 1);
		if (url[strlen(url) - 1] != '/')
			fputc('/', out);
		url_text(out, list[i]->d_name, 0);
		fputs("\">", out);
		html_text(out, list[i]->d_name);
		fputs("</a></li>\n", out);
		free(list[i]);
	}
	free(list);
	fputs("</ul></body></html>\n", out);
	if (ferror(out) || fflush(out) != 0 ||
	    (len = ftello(out)) < 0 || fseeko(out, 0, SEEK_SET) != 0) {
		fclose(out);
		send_error(fd, r, 500);
		return;
	}
	/* Generated listings are not treated as cacheable static files. */
	send_headers(fd, r, 200, "text/html; charset=utf-8", len, 0);
	while (!r->is_head && (n = fread(buf, 1, sizeof(buf), out)) > 0)
		if (write_body(fd, r, buf, n) < 0)
			break;
	fclose(out);
}

void
serve_path(int fd, struct request *r)
{
	char root[PATH_MAX], url[PATH_MAX], index[PATH_MAX], resolved[PATH_MAX];
	struct stat st;
	int status;

	r->sent = 0;
	status = resolve_path(r, root, url);
	if (status != 0) {
		send_error(fd, r, status);
		return;
	}
	if (stat(r->path, &st) < 0) {
		send_error(fd, r, file_error());
		return;
	}
	if (S_ISREG(st.st_mode)) {
		serve_file(fd, r, r->path);
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		send_error(fd, r, 403);
		return;
	}
	if (!join_path(index, sizeof(index), r->path, "index.html")) {
		send_error(fd, r, 400);
		return;
	}
	if (realpath(index, resolved) != NULL) {
		if (!inside_root(root, resolved))
			send_error(fd, r, 403);
		else
			serve_file(fd, r, resolved);
		return;
	}
	if (errno != ENOENT) {
		send_error(fd, r, file_error());
		return;
	}
	serve_directory(fd, r, url);
}
