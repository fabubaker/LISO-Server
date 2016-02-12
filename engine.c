/******************************************************************/
/* @file engine.c                                                 */
/*                                                                */
/* @brief Used to handle parsing and other heavy-lifting by LISO. */
/*                                                                */
/* @author Fadhil Abubaker                                        */
/******************************************************************/

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"

/*
@brief Parses a given buf based on state and populates
a struct with parsed tokens if successful. If request
is incomplete, stash it as its state.

@param state The saved state of the client

@retval  0    if successful
@retval -1    if incomplete request
@retval 400   if malformed
@retval 500   if internal error
@retval 505   if wrong version
*/
int parse_line(fsm* state)
{
  char* CRLF; char* tmpbuf;
  char* method; char* uri; char* version;
  size_t length;

  /* Check for CLRF */
  CRLF = memmem(state->request, state->end_idx, "\r\n\r\n", strlen("\r\n\r\n"));

  if(CRLF == NULL)
    return -1;

  /* We have a request, extract tokens */
  CRLF = memmem(state->request, state->end_idx,
                "\r\n", strlen("\r\n"));

  if(CRLF == NULL)
    return 500;

  /* Copy request line */
  length = (size_t)(CRLF - state->request);
  tmpbuf = strndup(state->request,length); // Remember to free here.

  /* Tokenize the line */
  method = strtok(tmpbuf," ");

  if (method == NULL)
  {free(tmpbuf); return 400;}

  /* Check if correct method */
  if (strncmp(method,"GET",strlen("GET")) && strncmp(method,"HEAD",strlen("HEAD"))
      && strncmp(method, "POST", strlen("POST")))
  {free(tmpbuf); return 501;}

  if((uri = strtok(NULL," ")) == NULL)
  {free(tmpbuf); return 400;}

  if((version = strtok(NULL, " ")) == NULL)
  {free(tmpbuf); return 400;}

  if(strncmp(version,"HTTP/1.1",strlen("HTTP/1.1")))
  {free(tmpbuf); return 505;}

  /* If there's one more token, malformed request */
  if(strtok(NULL," ") != NULL)
  {free(tmpbuf); return 400;}

  /* These are all malloced by strdup, so it is safe */
  state->method = method;
  state->uri = uri;
  state->version = version;

  return 0;
}

/*
  Currently parses only Content-Length for POST.

  @retval  0  Success
  @retval 411  No CL header (411)
  @retval 400  malformed request (400)
  @retval 500  internal error (500)

 */
int parse_headers(fsm* state)
{
  char* CRLF; char* tmpbuf; char* body_size;
  size_t length;

  CRLF = memmem(state->request, state->end_idx, "\r\n\r\n", strlen("\r\n\r\n"));

  if(CRLF == NULL)
    return 500;

  /* First extract Connection: */

  tmpbuf = memmem(state->request, state->end_idx, "Connection: close\r\n",
                  strlen("Connection: close\r\n"));

  if(tmpbuf == NULL)
    state->conn = 1; // Keep Alive
  else
    state->conn = 0; // Close

  /* Now extract Content-Length: if POST */

  if(strncmp(state->method,"POST",strlen("POST")))
  {
    state->header = (char*) 1; // For now
    return 0;
  }

  tmpbuf = memmem(state->request, state->end_idx, "Content-Length:",
                  strlen("Content-Length:"));

  if(tmpbuf == NULL)
    return 411;

  CRLF = memmem(tmpbuf, state->end_idx, "\r\n", strlen("\r\n"));
  length = (size_t)(CRLF - tmpbuf);
  tmpbuf = strndup(tmpbuf, length); // Free this guy please.

  if(strtok(tmpbuf," ") == NULL)
  {free(tmpbuf); return 411;}

  if((body_size = strtok(NULL, " ")) == NULL)
  {free(tmpbuf); return 411;}

  /* Check for valid Content-Length */
  if(!validsize(body_size))
  {free(tmpbuf); return 411;}

  /* If there's one more token, malformed request */
  if(strtok(NULL," ") != NULL)
  {free(tmpbuf); return 400;}

  /* insert code to check if actually a number */
  state->body_size = (size_t)atoi(body_size);
  free(tmpbuf);

  state->header = (char*) 1; // For now
  return 0;
}

/*
  For later...
 */
int parse_body()
{
  return 0;
}

/*
 */
int store_request(char* buf, int size, fsm* state)
{
  /* Check if request > 8192 */
  if(state->end_idx + size > 8192)
    return -2;

  /* Store away in fsm */
  memcpy(state->request+state->end_idx, buf, size);
  state->end_idx += size;

  return 0;
}
/*
  @retval  0  success
  @retval 500 internal server error
  @retval 404 File not found
 */
int service(fsm* state)
{
  struct tm *Date; time_t t;
  struct tm *Modified;
  struct stat meta;
  char timestr[200] = {0}; char type[40] = {0};
  char* response = state->response;
  FILE *file;

  int pathlength = strlen(state->uri) + strlen(state->www) + strlen("/") + 1;
  char* path = malloc(pathlength);
  memset(path,0,pathlength);

  if(!strncmp(state->uri, "/", strlen("/")) && strlen(state->uri) == 1)
  {
    strncat(path,state->www,strlen(state->www));
    strncat(path,"/",strlen("/"));
    strncat(path,"index.html",strlen("index.html"));
  }
  else
  {
    strncat(path,state->www,strlen(state->www));
    strncat(path,"/",strlen("/"));
    strncat(path,state->uri,strlen(state->uri));
  }

  t = time(NULL);
  Date = gmtime(&t);

  if (Date == NULL)
  {
    return 500;
  }

  if(!strncmp(state->method,"GET",strlen("GET")) ||
     !strncmp(state->method,"HEAD",strlen("HEAD")))
  {
    /* Grab Date of message */
    if(strftime(timestr, 200, "%a, %d %b %Y %H:%M:%S %Z" ,Date) == 0)
    {
      return 500;
    }

    /* Check if file exists */
    if(stat(path, &meta) == -1)
    {
      return 404;
    }

    if(!strncmp(state->method, "GET",strlen("GET")))
    {
      /* Open uri specified by client and save it in state*/
      file = fopen(path,"r");
      state->body = malloc(meta.st_size);
      state->body_size = meta.st_size;
      fread(state->body,1,state->body_size,file);
    }
    else
    {
      state->body = NULL;
      state->body_size = 0;
    }

    Modified = gmtime(&meta.st_mtime);

    sprintf(response, "HTTP/1.1 200 OK\r\n");
    sprintf(response, "%sDate: %s\r\n", response,timestr);
    sprintf(response, "%sServer: Liso/1.0\r\n", response);

    if(!state->conn)
      sprintf(response, "%sConnection: close\r\n", response);
    else
      sprintf(response, "%sConnection: keep-alive\r\n", response);

    if(mimetype(state->uri, strlen(state->uri),type))
      sprintf(response, "%sContent-Type: %s\r\n", response, type);

    sprintf(response, "%sContent-Length: %jd\r\n", response, meta.st_size);
    memset(timestr, 0, 200);

    if(strftime(timestr, 200, "%a, %d %b %Y %H:%M:%S %Z" , Modified) == 0)
    {
      return 500;
    }

    sprintf(response, "%sLast-Modified: %s\r\n\r\n", response, timestr);
    state->resp_idx = (int)strlen(response);
  }
  else
  {
    state->body = NULL;
    state->body_size = 0;

    if(strftime(timestr, 200, "%a, %d %b %Y %H:%M:%S %Z" ,Date) == 0)
    {
      return 500;
    }

    sprintf(response, "HTTP/1.1 200 OK\r\n");
    sprintf(response, "%sDate: %s\r\n", response,timestr);
    sprintf(response, "%sServer: Liso/1.0\r\n\r\n", response);
    state->resp_idx = (int)strlen(response);
  }

  free(path);
  return 0;
}

/*
 *
 * @retval 0 if unrecognizable
 * @retval 1 if successful
 *
 */
int mimetype(char* file, size_t len, char* type)
{
  char* ext; size_t extlen;

  ext = memmem(file, len, ".", strlen("."));

  if(ext == NULL)
    return 0;

  extlen = len - (size_t)((ext + 1) - file);

  if((size_t)((ext+1) - file) == len)
  {
    return 0;
  }

  memcpy(type, ext+1, extlen);

  if(!strncmp(type,"html",strlen("html")))
  {
    sprintf(type, "text/html"); return 1;
  }

  if(!strncmp(type,"css",strlen("css")))
  {
    sprintf(type, "text/css"); return 1;
  }

  if(!strncmp(type,"png",strlen("png")))
  {
    sprintf(type, "image/png"); return 1;
  }

  if(!strncmp(type,"jpeg",strlen("jpeg")))
  {
    sprintf(type, "image/jpeg"); return 1;
  }

  if(!strncmp(type,"gif",strlen("gif")))
  {
    sprintf(type, "image/gif"); return 1;
  }

  return 0;
}

int validsize(char* body_size)
{
  int size = atoi(body_size);

  if(size < 0)
    return 0;
  else
    return 1;
}

/*
 *
 * @returns length of new end
 */
int resetbuf(char* buf, int end)
{
  char* CRLF; char* next;
  size_t length;

  /* Since we've serviced at least one request, CRLF should show up */
  CRLF = memmem(buf, end, "\r\n\r\n", strlen("\r\n\r\n"));

  if (CRLF == NULL)
    return -1;

  /* length of the 1st request including CRLF */
  length = (size_t)(strlen("\r\n\r\n") + CRLF - buf);

  if(length >= 8192)
  {
    /* buf was full, just memset */
    memset(buf, 0, BUF_SIZE);
    return 0;
  }

  next = CRLF+4;

  /* Copy remaining requests to start of buf */
  memcpy(buf,next,BUF_SIZE-length);

  /* Zero out the rest of the buf */
  memset(buf+(BUF_SIZE-length),0,length);

  return strlen(buf);
}

void clean_state(fsm* state)
{
  memset(state->response, 0, BUF_SIZE);
  state->header = NULL;

  if(state->method != NULL)
    free(state->method);

  state->method = NULL;
  state->uri = NULL;
  state->version = NULL;

  if(state->body != NULL)
    free(state->body);

  state->body = NULL;
  state->body_size = 0;

  state->resp_idx = 0;
}

/**********************************************************/
/* @returns NULL If not needle not found; else pointer to */
/* first occurrence of needle                             */
/* Code from stackoverflow.                               */
/**********************************************************/
void *memmem(const void *haystack, size_t hlen,
             const void *needle, size_t nlen)
{
    int needle_first;
    const void *p = haystack;
    size_t plen = hlen;

    if (!nlen)
        return NULL;

    needle_first = *(unsigned char *)needle;

    while (plen >= nlen && (p = memchr(p, needle_first, plen - nlen + 1)))
    {
        if (!memcmp(p, needle, nlen))
            return (void *)p;

        p++;
        plen = hlen - (p - haystack);
    }

    return NULL;
}
