# Compiler options.
CC = gcc
CFLAGS = -Wall -Werror

all			: webserver

webserver	: webserver.c
			$(CC) $(CFLAGS) -o webserver webserver.c

clean:
	rm webserver
