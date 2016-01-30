#########################################################
# Makefile											    #
# 													    #
# Description: Makefile for compiling the lisod server. #
# 													    #
# Author: Fadhil Abubaker.							    #
#########################################################

CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -std=gnu99

all: lisod

lisod: lisod.c logger.o
	$(CC) $(CFLAGS) lisod.c logger.o -o lisod

logger: logger.h logger.c
	$(CC) $(CFLAGS) logger.c -o logger.o

handin:
	(make clean; tar cvf 15-441-project-1.tar -T handin.txt)

echo_client:
	$(CC) $(CFLAGS) echo_client.c -o echo_client

.PHONY: all clean

clean:
	rm -f *~ *.o *.tar lisod
