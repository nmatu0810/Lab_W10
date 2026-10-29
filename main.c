#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sws.h"

struct config cfg = { NULL, NULL, NULL, NULL, DEFAULT_PORT, 0 };

void server_loop(void);

static void
usage(void)
{
	fprintf(stderr, "usage: sws [-dh] [-c dir] [-i address] "
	    "[-l file] [-p port] dir\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "c:dhi:l:p:")) != -1) {
		switch (ch) {
		/* TODO */
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	cfg.docroot = argv[0];

	/* TODO: check docroot, daemon() neu khong co -d */

	server_loop();
	return EXIT_SUCCESS;
}
