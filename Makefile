CC      = cc
CFLAGS  = -Wall -Werror -Wextra -g -D_GNU_SOURCE
LDFLAGS =
PROG    = sws
OBJS    = main.o net.o request.o response.o serve.o cgi.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): sws.h

test: $(PROG)
	./test.sh

clean:
	rm -f $(PROG) $(OBJS)

.PHONY: all test clean
