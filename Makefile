#########################################################
# Makefile											    #
# 													    #
# Description: Makefile for compiling the lisod server. #
# 													    #
# Author: Fadhil Abubaker.							    #
#########################################################

CC		= gcc
CFLAGS 	= -Wall -Wextra -Werror -g -std=gnu99
SSL  	= -lssl -lcrypto

all: lisod

lisod: lisod.c logger.o engine.o
	$(CC) $(CFLAGS) lisod.c logger.o engine.o -o lisod $(SSL)

logger: logger.h logger.c
	$(CC) $(CFLAGS) logger.c -o logger.o

engine: engine.h engine.c
		$(CC) $(CFLAGS)  engine.c -o engine.o $(SSL)

handin:
	(make clean; cd ..; tar cvf fabubake.tar 15-441-project-1 --exclude cp1_checker.py --exclude starter_code --exclude www --exclude handin.txt --exclude yolo --exclude ".gdbinit" --exclude ".gitignore");

test: lisod
	./lisod 9999 9998 logfile lockfile www cgipath grader.key grader.crt

echo_client:
	$(CC) $(CFLAGS) echo_client.c -o echo_client

.PHONY: all clean

clean:
	rm -f *~ *.o *.tar lisod
