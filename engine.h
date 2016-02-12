#ifndef ENGINE_H
#define ENGINE_H

#include "lisod.h"

int   parse_line(fsm* state);
int   parse_headers(fsm* state);
int   store_request(char* buf, int size, fsm* state);
int   service(fsm* state);
void* memmem(const void *haystack, size_t hlen,
             const void *needle, size_t nlen);

int  resetbuf(char* buf, int end);
void clean_state(fsm* state);
int  mimetype(char* file, size_t len, char* type);
int  validsize(char* body_size);

int Recv(int fd, SSL* client_context, char* buf, int num);
int Send(int fd, SSL* client_context, char* buf, int num);

void addtofree   (char** freebuf, char* ptr, int bufsize);
void delfromfree (char** freebuf, int bufsize);

#endif
