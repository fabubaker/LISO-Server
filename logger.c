/*******************************************************************/
/*                                                                 */
/* @file logger.c                                                  */
/*                                                                 */
/* @brief Logger module to be used with liso. Outputs logging data */
/* to a specified file while handling errors.                      */
/*                                                                 */
/* @author Fadhil Abubaker                                         */
/*                                                                 */
/*******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "logger.h"

FILE* log_open(char* filename)
{
  FILE* file = fopen(filename,"w+");

  if(file == NULL)
  {
    fprintf(stderr,"Error opening/creating log file. \n");
    exit(1);
  }

  return file;
}

int log_close(FILE* file)
{
  fprintf(file, "Closing log...\n");

  if(fclose(file) != 0)
  {
    fprintf(stderr,"Error closing log file. \n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int log_error(char* error, FILE* file)
{
  time_t now;
  time(&now);

  fprintf(file, "%s%s \n \n", ctime(&now), error);

  return EXIT_SUCCESS;
}

/* /\* */
/*  * @brief Returns an error message to the client. */
/*  *\/ */
/* void clienterror(int fd, char *cause, char *errnum, */
/* 		 char *shortmsg, char *longmsg) */
/* { */
/*     char buf[BUF_SIZE], body[BUF_SIZE]; */

/*     /\* Build the HTTP response body *\/ */
/*     sprintf(body, "<html><title>Proxy Error!</title>"); */
/*     sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body); */
/*     sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); */
/*     sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); */
/*     sprintf(body, "%s<hr><em>Liso Web Server says hi </em>\r\n", body); */

/*     /\* Print the HTTP response *\/ */
/*     sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); */
/*     Rio_writen(fd, buf, strlen(buf)); */
/*     sprintf(buf, "Content-type: text/html\r\n"); */
/*     Rio_writen(fd, buf, strlen(buf)); */
/*     sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body)); */
/*     Rio_writen(fd, buf, strlen(buf)); */
/*     Rio_writen(fd, body, strlen(body)); */
/* } */
/* *\/ */
