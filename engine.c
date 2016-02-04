/******************************************************************/
/* @file engine.c                                                 */
/*                                                                */
/* @brief Used to handle parsing and other heavy-lifting by LISO. */
/*                                                                */
/* @author Fadhil Abubaker                                        */
/******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"

/**********************************************************/
/* @brief Parses a given buf based on state and populates */
/* a struct with parsed tokens if successful. If request  */
/* is incomplete, stash it as its state.                  */
/*                                                        */
/* @param state The saved state of the client             */
/*                                                        */
/* @retval  0  if successful                              */
/* @retval -1  if incomplete request                      */
/* @retval -2  if malformed                               */
/**********************************************************/
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
    return -2;

  /* Copy request line */
  length = (size_t)(CRLF - state->request);
  tmpbuf = strndup(state->request,length); // Remember to free here.

  /* Tokenize the line */
  method = strtok(tmpbuf," ");

  if (method == NULL)
    return -2;

  /* Check if correct method */
  if (strncmp(method,"GET",strlen("GET")) && strncmp(method,"HEAD",strlen("HEAD"))
      && strncmp(method, "POST", strlen("POST")))
    return -2;

  if((uri = strtok(NULL," ")) == NULL)
    return -2;

  if((version = strtok(NULL, " ")) == NULL)
    return -2;

  if(strncmp(version,"HTTP/1.1",strlen("HTTP/1.1")))
    return -2;

  /* If there's one more token, malformed request */
  if(strtok(NULL," ") != NULL)
    return -2;

  /* These are all malloced by strdup */
  state->method = method;
  state->uri = uri;
  state->version = version;

  return 0;
}

/*
  Currently parses only Content-Length for POST.

  @retval  0  Success
  @retval -2  Unrecoverable error
  @retval -1  No CL header
 */
int parse_headers(fsm* state)
{
  if(strncmp(state->method,"POST", strlen("POST")))
  {
    state->header = (char *)1; // make the variable not NULL
    return 0;
  }

  char* CRLF; char* tmpbuf; char* body_size;
  size_t length;

  CRLF = memmem(state->request, state->end_idx, "\r\n\r\n", strlen("\r\n\r\n"));

  if(CRLF == NULL)
    return -2;

  tmpbuf = memmem(state->request, state->end_idx, "Content-Length:",
                  strlen("Content-Length:"));

  if(tmpbuf == NULL)
    return -1;

  CRLF = memmem(tmpbuf, state->end_idx, "\r\n", strlen("\r\n"));
  length = (size_t)(CRLF - tmpbuf);
  tmpbuf = strndup(tmpbuf, length); // Free this guy please.

  if(strtok(tmpbuf," ") == NULL)
    return -1;

  if((body_size = strtok(NULL, " ")) == NULL)
    return -1;

  /* If there's one more token, malformed request */
  if(strtok(NULL," ") != NULL)
    return -2;

  /* insert code to check if actually a number */
  state->body_size = (size_t)atoi(body_size);

  return 0;
}

/*

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
  strncpy(state->request+state->end_idx, buf, size);
  state->end_idx += size;

  return 0;
}
/*
  @retval  0  success
  @retval -1  unrecoverable error
  @retval -2  404
 */
int service(fsm* state)
{
  struct tm *tmp; time_t t;
  char* response = state->response;
  char timestr[200] = {0};

  t = time(NULL);
  tmp = gmtime(&t);
  if (tmp == NULL)
  {
    return -1;
  }

  if(!strncmp(state->method,"GET",strlen("GET")) ||
     !strncmp(state->method,"HEAD",strlen("HEAD")))
  {
    if(strftime(timestr, 200, "%a, %d %b %y %T %z" ,tmp) == 0)
    {
      return -1;
    }

    sprintf(response, "HTTP/1.1 200 OK\r\n");
    sprintf(response, "%sDate: %s \r\n", response,timestr);
    sprintf(response, "%sServer: Liso/1.0\r\n\r\n", response);
    state->resp_idx = (int)strlen(response);
  }

  return 0;
}

void client_error(fsm* state, char* errnum, char* errormsg)
{
  char* response = state->response;
  char body[LOG_SIZE] = {0};

  memset(response,0,BUF_SIZE);

  /* Build the HTTP response body */
  sprintf(response, "HTTP/1.0 %s %s\r\n", errnum, errormsg);
  sprintf(response, "%sContent-type: text/html\r\n", response);

  sprintf(body, "<html><title>Webserver Error!</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, errormsg);
  sprintf(body, "%s<hr><em>Fadhil's Web Server </em>\r\n", body);

  sprintf(response, "%sContent-length: %d\r\n\r\n", response,(int)strlen(body));
  sprintf(response, "%s%s", response, body);

  state->resp_idx += strlen(response);
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
