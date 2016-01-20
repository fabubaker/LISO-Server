#########################################################
# Makefile											    #
# 													    #
# Description: Makefile for compiling the lisod server. #
# 													    #
# Author: Fadhil Abubaker.							    #
#########################################################

default: lisod

lisod:
	@gcc lisod.c -g -o lisod -Wall -Werror -Wextra

echo_client:
	@gcc echo_client.c -g -o echo_client -Wall -Werror -Wextra

clean:
	@rm lisod
