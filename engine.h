#ifndef ENGINE_H
#define ENGINE_H

#include "lisod.h"

int parse_line(fsm* state);
int parse_headers(fsm* state);
int store_request(char* buf, int size, fsm* state);
int service(fsm* state);
void *memmem(const void *haystack, size_t hlen,
             const void *needle, size_t nlen);
void client_error(fsm* state, char* errnum, char* errormsg);
int resetbuf(char* buf, int end);
void clean_state(fsm* state);
#endif
