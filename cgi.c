#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sws.h"

int
is_cgi(const struct request *r)
{
	/* TODO */
	(void)r;
	return 0;
}

void
serve_cgi(int fd, struct request *r, const struct sockaddr *client)
{
	/* TODO: fork + execve, set bien moi truong CGI */
	(void)client;
	send_error(fd, r, 501);
}
