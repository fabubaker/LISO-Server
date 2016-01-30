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

lisod: lisod.c
	$(CC) $(CFLAGS) lisod.c -o lisod

handin:
	(make clean; tar cvf 15-441-project-1.tar -T handin.txt)

echo_client:
	$(CC) $(CFLAGS) echo_client.c -o echo_client

.PHONY: all clean

clean:
	rm -f *~ *.o *.tar lisod
