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

echo_client:
	$(CC) $(CFLAGS) echo_client.c -o echo_client

.PHONY: all clean

clean:
	rm -f *~ *.o lisod
