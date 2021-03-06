/********************************************************************************/
/* @file lisod.c                                                                */
/*                                                                              */
/* @brief A simple web server that uses select() to handle                      */
/* multiple concurrent clients.                                                 */
/*                                                                              */
/* Supports SSL/TLS, CGI, GET, HEAD and POST requests.                          */
/*                                                                              */
/* @author Fadhil Abubaker                                                      */
/*                                                                              */
/* @usage: ./lisod <HTTP port> <HTTPS port> <log file> <lock file> <www folder> */
/* <CGI script path> <privatekey file> <certificate file>                       */
/********************************************************************************/


/* Part of the code is based on the select-based echo server found in
   CSAPP */


/* YOLO M8s */

#include <mcheck.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>


/* OpenSSL headers */
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "lisod.h"
#include "logger.h"
#include "engine.h"

/** Global vars **/
FILE* logfile;   /* Legitimate use of globals, I swear! */

char* wwwfolder;
char* cgipath;
short listen_port;
short https_port;

/** Prototypes **/

int  close_socket(int sock);
void init_pool(int listenfd, int https_fd, pool *p);
void add_client(int client_fd, char* wwwfolder, SSL* client_context, pool *p);
void check_clients(pool *p);
void cleanup(int sig);
void sigchld_handler(int sig);
int daemonize(char* lock_file);

/** Definitions **/
void no_op(enum mcheck_status status) {status = status;}

int main(int argc, char* argv[])
{
  if (argc != 6 && argc != 9) // argc = 9
  {
    fprintf(stderr, "%d \n", argc);
    fprintf(stderr, "usage: %s <HTTP port> <HTTPS port> <log file> ", argv[0]);
    fprintf(stderr, "<lock file> <www folder> <CGI script path> ");
    fprintf(stderr, "<privatekey file> <certificate file> \n");
    return EXIT_FAILURE;
  }

  mcheck(&no_op);

  /* Ignore SIGPIPE */
  /* Handle SIGINT to cleanup after liso */
  /* Install SIGCHLD handler to reap children */
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT,  cleanup);
  signal(SIGCHLD, sigchld_handler);

  /* Parse cmdline args */
  listen_port       = atoi(argv[1]);
  https_port        = atoi(argv[2]);
  logfile           = log_open(argv[3]);
  //char* lockfile    = argv[4];
  wwwfolder         = argv[5];
  cgipath           = argv[6];
  char* privatekey  = argv[7];
  char* certfile    = argv[8];

  /* Various buffers for read/write */
  char log_buf[LOG_SIZE]            = {0};
  char hostname[LOG_SIZE]           = {0};
  char cli_ip[INET_ADDRSTRLEN]      = {0};
  char port[10]                     = {0};

  int                 listen_fd, https_fd, client_fd;
  socklen_t           cli_size;
  struct sockaddr_in  serv_addr, https_addr, cli_addr;
  pool *pool =        malloc(sizeof(struct pool));
  struct timeval      tv;
  tv.tv_sec = 5;

  /* SSL variables */
  SSL     *client_context = NULL;
  SSL_CTX *ssl_context;

  /*** Begin daemonizing ***/
  // daemonize(lockfile);

  /********* BEGIN INIT *******/
  SSL_library_init();
  SSL_load_error_strings();

  /* we want to use TLSv1 only */
  if ((ssl_context = SSL_CTX_new(TLSv1_server_method())) == NULL)
  {
    fprintf(stderr, "Error creating SSL context.\n");
    return EXIT_FAILURE;
  }

  /* register private key */
  if (SSL_CTX_use_PrivateKey_file(ssl_context, privatekey,
                                  SSL_FILETYPE_PEM) == 0)
  {
    SSL_CTX_free(ssl_context);
    fprintf(stderr, "Error associating private key.\n");
    return EXIT_FAILURE;
  }

  /* register public key (certificate) */
  if (SSL_CTX_use_certificate_file(ssl_context, certfile,
                                   SSL_FILETYPE_PEM) == 0)
  {
    SSL_CTX_free(ssl_context);
    fprintf(stderr, "Error associating certificate.\n");
    return EXIT_FAILURE;
  }

  if(pool == NULL)
  {
    log_error("Malloc error! Exiting!", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  fprintf(stdout, "-----Welcome to Liso!-----\n");

  /* all networked programs must create a socket */
  if ((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1 ||
      (https_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
  {
    SSL_CTX_free(ssl_context);
    log_error("Failed creating socket.",logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  serv_addr.sin_family        = AF_INET;
  serv_addr.sin_port          = htons(listen_port);
  serv_addr.sin_addr.s_addr   = INADDR_ANY;

  https_addr.sin_family       = AF_INET;
  https_addr.sin_port         = htons(https_port);
  https_addr.sin_addr.s_addr  = INADDR_ANY;

  /* Set sockopt so that ports can be resued */
  int enable = -1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) == -1 ||
      setsockopt(https_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) == -1)
  {
    SSL_CTX_free(ssl_context);
    close_socket(listen_fd);
    close_socket(https_fd);
    log_error("setsockopt error! Aborting...", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  /* servers bind sockets to ports---notify the OS they accept connections */
  if (bind(listen_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) ||
      bind(https_fd, (struct sockaddr *) &https_addr, sizeof(https_addr)))
  {
    close_socket(listen_fd);
    close_socket(https_fd);
    log_error("Failed binding socket.", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  if (listen(https_fd, 5) || listen(listen_fd, 5))
  {
    close_socket(https_fd);
    close_socket(listen_fd);
    SSL_CTX_free(ssl_context);
    log_error("Error listening on socket.", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  /* Initialize our pool of fds */
  init_pool(listen_fd, https_fd, pool);

  /******** END INIT *********/

  /******* BEGIN SERVER CODE ******/

  /* finally, loop waiting for input and then write it back */
  while (1)
  {
    /* Block until there are file descriptors ready */
    pool->readfds = pool->masterfds;
    pool->writefds = pool->masterfds;

    if((pool->nready = select(pool->maxfd+1, &pool->readfds, &pool->writefds,
                              NULL, &tv)) == -1)
    {
      close_socket(listen_fd);
      memset(log_buf, 0, LOG_SIZE);
      sprintf(log_buf, "Select failed! Error: %s", strerror(errno));
      log_error(log_buf,logfile);
      log_close(logfile);
      return EXIT_FAILURE;
    }

    /* Is the http port having clients ? */
    if (FD_ISSET(listen_fd, &pool->readfds))
    {
      cli_size = sizeof(cli_addr);
      if ((client_fd = accept(listen_fd, (struct sockaddr *) &cli_addr,
                              &cli_size)) == -1)
      {
        close(listen_fd);
        log_error("Error accepting connection.", logfile);
        log_close(logfile);
        return EXIT_FAILURE;
      }

      /* Log client data */
      getnameinfo((struct sockaddr *) &cli_addr, cli_size,
                  hostname, LOG_SIZE, port, 10, 0);
      memset(log_buf, 0, LOG_SIZE);
      sprintf(log_buf,
              "We have a new client: Say hi to %s:%s.", hostname, port);
      log_error(log_buf, logfile);

      inet_ntop(AF_INET, &(cli_addr.sin_addr), cli_ip, INET_ADDRSTRLEN);

      add_client(client_fd, cli_ip, NULL, pool);
    }

    /* Is the https port having clients ? */
    if (FD_ISSET(https_fd,  &pool->readfds))
    {
      cli_size = sizeof(cli_addr);
      if ((client_fd = accept(https_fd, (struct sockaddr *) &cli_addr,
                              &cli_size)) == -1)
      {
        close(https_fd);
        SSL_CTX_free(ssl_context);
        log_error("Error accepting connection.", logfile);
        log_close(logfile);
        return EXIT_FAILURE;
      }

      /************ WRAP SOCKET WITH SSL ************/
      if ((client_context = SSL_new(ssl_context)) == NULL)
      {
        close(https_fd);
        SSL_CTX_free(ssl_context);
        fprintf(stderr, "Error creating client SSL context.\n");
        return EXIT_FAILURE;
      }

      if (SSL_set_fd(client_context, client_fd) == 0)
      {
        close(https_fd);
        SSL_free(client_context);
        SSL_CTX_free(ssl_context);
        fprintf(stderr, "Error creating client SSL context.\n");
        return EXIT_FAILURE;
      }

      if (SSL_accept(client_context) <= 0)
      {
        close(https_fd);
        SSL_free(client_context);
        SSL_CTX_free(ssl_context);
        fprintf(stderr, "Error accepting (handshake) client SSL context.\n");
        return EXIT_FAILURE;
      }
      /************ END WRAP SOCKET WITH SSL ************/

      /* Log client data */
      getnameinfo((struct sockaddr *) &cli_addr, cli_size,
                  hostname, LOG_SIZE, port, 10, 0);
      memset(log_buf, 0, LOG_SIZE);
      sprintf(log_buf,
              "We have a new SSL client: Say hi to %s:%s.", hostname, port);
      log_error(log_buf, logfile);
      add_client(client_fd, wwwfolder, client_context, pool);
    }

    /* Read and respond to each client requests */
    check_clients(pool);
  }
}

int close_socket(int sock)
{
  if (close(sock))
  {
    log_error("Failed closing socket", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

/*
 * @brief Initializes the pool struct so that it contains only the listenfd
 *
 * @param listenfd The socket for listening for new connections.
 * @paran p        The pool struct to initialize.
 */
void init_pool(int listenfd, int https_fd, pool *p)
{
  p->maxi = -1;

  memset(p->clientfd, -1, FD_SETSIZE*sizeof(int)); // No clients at the moment.
  //memset(p->data,      0, FD_SETSIZE*BUF_SIZE*sizeof(char)); // No data yet.
  memset(p->states,    0, FD_SETSIZE*sizeof(fsm*)); // NULL out the fsms.

  /* Initailly, listenfd and https_fd are the only members of the read set */
  p->maxfd = https_fd;
  FD_ZERO(&p->masterfds);
  FD_SET(listenfd, &p->masterfds);
  FD_SET(https_fd, &p->masterfds);
}

/*
 * @brief Adds a client file descriptor to the pool and updates it.
 *
 * @param client_fd The client file descriptor.
 * @param p         The pool struct to update.
 */
void add_client(int client_fd, char* cli_ip, SSL* client_context, pool *p)
{
  int i; fsm* state;

  p->nready--;

  /* Create a fsm for this client */
  state = malloc(sizeof(struct state));

  /* Create initial values for fsm */
  memset(state->request,  0, BUF_SIZE);
  memset(state->response, 0, BUF_SIZE);
  state->method     = NULL;
  state->uri        = NULL;
  state->version    = NULL;
  state->header     = NULL;
  state->body       = NULL;
  state->body_size  = -1; // No body as of yet

  state->end_idx    = 0;
  state->resp_idx   = 0;

  state->www            = wwwfolder;
  state->conn           = 1;
  state->context = client_context;
  strncpy(state->cli_ip, cli_ip, INET_ADDRSTRLEN);

  state->pipefds    = -1;

  memset(state->freebuf, 0, FREE_SIZE*sizeof(char*));

  for (i = 0; i < FD_SETSIZE; i++)  /* Find an available slot */
  {
    if(p->clientfd[i] < 0) {   /* Found one free slot */
      p->clientfd[i] = client_fd;

      /* Add the descriptor to the master set */
      FD_SET(client_fd, &p->masterfds);

      /* Add fsm to pool */
      p->states[i] = state;

      /* Update max descriptor and max index */
      if (client_fd > p->maxfd)
        p->maxfd = client_fd;
      if (i > p->maxi)
        p->maxi = i;
      break;
    }
  }

  if (i == FD_SETSIZE)   /* There are no empty slots */
  {
    client_error(state, 503);
    send(client_fd, state->response, state->resp_idx, 0);
    log_error("Too many clients! Closing client socket...", logfile);
    close_socket(client_fd);
    free(state);
  }
}

/*
  Makes a copy of the client's fsm struct.
 */
int add_cgi(int client_fd, fsm* state, pool* p)
{
  int i; fsm* cgi;

  /* Create a fsm for this cgi process */
  cgi = malloc(sizeof(struct state));

  /* Create initial values for fsm */
  memset(cgi->request,  0, BUF_SIZE);
  memset(cgi->response, 0, BUF_SIZE);
  strncpy(cgi->response, state->response, state->resp_idx);

  cgi->method     = NULL;
  cgi->uri        = NULL;
  cgi->version    = NULL;
  cgi->header     = NULL;
  cgi->body       = NULL;
  cgi->body_size  = 0; // No body as of yet

  cgi->end_idx    = 0;
  cgi->resp_idx   = state->resp_idx;

  cgi->www            = wwwfolder;
  cgi->conn           = 1;
  cgi->context        = state->context;
  // Save the client fd to write cgi data back to..
  cgi->pipefds        = state->pipefds;

  memset(cgi->freebuf, 0, FREE_SIZE*sizeof(char*));

  for (i = 0; i < FD_SETSIZE; i++)  /* Find an available slot */
  {
    if(p->clientfd[i] < 0) {   /* Found one free slot */
      p->clientfd[i] = client_fd;

      /* Add the descriptor to the master set */
      FD_SET(state->pipefds, &p->masterfds);

      /* Add fsm to pool */
      p->states[i] = cgi;

      /* Update max descriptor and max index */
      if (state->pipefds > p->maxfd)
        p->maxfd = state->pipefds;
      if (i > p->maxi)
        p->maxi = i;
      break;
    }
  }

  if (i == FD_SETSIZE)   /* There are no empty slots */
  {
    client_error(state, 500);
    Send(client_fd, state->context, state->response, state->resp_idx);
    free(cgi);
    return -1;
  }

  state->pipefds = -1;
  return 0;
}

/*********************************************************************/
/* @brief Iterates through active clients and reads requests.        */
/*                                                                   */
/* Uses select to determine whether clients are ready for reading or */
/* writing, reads a request. Never blocks for a                      */
/* single user.                                                      */
/*                                                                   */
/* @param p The pool of clients to iterate through.                  */
/*********************************************************************/
void check_clients(pool *p)
{
  int i, client_fd, cgi_fd, n, error;
  fsm* state;
  char buf[BUF_SIZE] = {0}; char log_buf[LOG_SIZE] = {0};

  memset(buf,0,BUF_SIZE);

  /* Iterate through all clients, and read their data */
  for(i = 0; (i <= p->maxi) && (p->nready > 0); i++)
  {
    if((client_fd = p->clientfd[i]) <= 0)
      continue;
    state     = p->states[i];
    cgi_fd    = state->pipefds;

    /* Check first for a CGI process to be read from, if any */
    if (client_fd > 0 && state->pipefds > 0 &&
        FD_ISSET(state->pipefds, &p->readfds))
    {
      cgi_fd = state->pipefds;
      /* Check if cgi process is ready to be read */
      if(cgi_fd > 0 && FD_ISSET(cgi_fd, &p->readfds))
      {
        /* receive bytes from the cgi process */
        n = read(cgi_fd, buf, BUF_SIZE);

        fprintf(stderr,"%s", buf);

        /* We received some bytes, store em*/
        if(n >= 1)
        {
          store_request(buf, n, state);
        }

        /* CGI process performed orderly shutdown */
        if(n == 0)
        {
          if(Send(client_fd, state->context, state->request, state->end_idx)
             != state->end_idx)
          {
            rm_client(client_fd, p, "CGI process failed", i);
          }
          /* Done with CGI, remove cgifd from select */
          rm_cgi(cgi_fd, p, "CGI iz dun", i);
        }

        /* Error reading from CGI process */
        if(n == -1)
        {
          rm_client(client_fd, p, "CGI process failed", i);
        }
      }
      continue;
    }

    if(state->pipefds > 0) continue; // This is a CGI fd, do not let it go below

    /* If a descriptor is ready to be read, read a line from it */
    if (client_fd > 0 && FD_ISSET(client_fd, &p->readfds))
    {
      p->nready--;

      state = p->states[i];

      /* Recv bytes from the client */
      n = Recv(client_fd, state->context, buf, BUF_SIZE);

      /* We have received bytes, send for parsing. */
      if (n >= 1)
      {
        store_request(buf, n, state);

        /* The loop that keeps servicing pipelined request */
        do{
          /* First, parse method, URI and version. */
          if(state->method == NULL)
          {
            /* Malformed Request */
            if((error = parse_line(state)) != 0 && error != -1)
            {
              client_error(state, error);
              if (Send(client_fd, state->context, state->response, state->resp_idx)
                  != state->resp_idx)
              {
                rm_client(client_fd, p, "Unable to write to client", i);
                break;
              }
              rm_client(client_fd, p, "HTTP error", i);
              break;
            }

            /* Incomplete request, save and continue to next client */
            if(error == -1) break;
          }

          /* Then, parse headers. */
          if(state->header == NULL && state->method != NULL)
          {
            if((error = parse_headers(state)) != 0)
            {
              client_error(state, error);
              if (Send(client_fd, state->context, state->response, state->resp_idx) !=
                  state->resp_idx)
              {
                rm_client(client_fd, p, "Unable to write to client", i);
                break;
              }
              rm_client(client_fd, p, "HTTP error", i);
              break;
            }
          }

          /* If POST, parse the body */
          if(!strncmp(state->method, "POST", strlen("POST")) &&
             state->body == NULL)
          {
            if((error = parse_body(state)) != 0 && error != -1)
            {
              client_error(state, error);
              if (Send(client_fd, state->context, state->response, state->resp_idx) !=
                  state->resp_idx)
              {
                rm_client(client_fd, p, "Unable to write to client", i);
                break;
              }
              rm_client(client_fd, p, "HTTP error", i);
              break;
            }

            /* Incomplete request, save and continue to next client */
            if(error == -1) break;
          }

          /* If everything has been parsed, write to client */
          if(state->method != NULL && state->header != NULL)
          {
            if ((error = service(state)) != 0)
            {
              client_error(state, error);
              if (Send(client_fd, state->context, state->response, state->resp_idx) !=
                  state->resp_idx)
              {
                rm_client(client_fd, p, "Unable to write to client", i);
                break;
              }
              rm_client(client_fd, p, "HTTP error", i);
              break;
            }

            /* if POST/GET CGI */
            if(state->pipefds > 0)
            {
              if(add_cgi(client_fd, state, p))
              {
                rm_client(client_fd, p, "Too many processes", i);
                break;
              }
            }
            /* Regular GET/HEAD */
            else if (Send(client_fd, state->context, state->response, state->resp_idx)
                != state->resp_idx ||
                Send(client_fd, state->context, state->body, state->body_size)
                != state->body_size)
            {
              rm_client(client_fd, p, "Unable to write to client", i);
              break;
            }

            else
            {
              memset(log_buf,0,LOG_SIZE);
              sprintf(log_buf,"Sent %d bytes of data!",
                      state->resp_idx+(int)state->body_size);
              log_error(log_buf,logfile);
            }
            memset(buf,0,BUF_SIZE);
          }

          /* Finished serving one request, reset buffer */
          state->end_idx = resetbuf(state);
          clean_state(state);
          if(!state->conn) rm_client(client_fd, p, "Connection: close", i);
        } while(error == 0 && state->conn);
        continue;
      }

      /* Client sent EOF, close socket. */
      if (n == 0)
      {
        rm_client(client_fd, p, "Client closed connection with EOF", i);
      }

      /* Error with recv */
      if (n == -1)
      {
        rm_client(client_fd, p, "Error reading from client socket", i);
      }
    } // End of client read check
  } // End of client loop.
}

/********************************************************************/
/* @brief Removes a CGI socket from the pool of states and clients, */
/*   freeing up resources and cleaning up memory.                   */
/*                                                                  */
/* @param cgi_fd  The CGI fd to be removed from the pool            */
/* @param p       The pool from which cgi_fd is to be removed       */
/* @param logmsg  message to write to logfile                       */
/* @param i       The index into the pool                           */
/********************************************************************/
void rm_cgi(int cgi_fd, pool* p, char* logmsg, int i)
{
  fsm* state = p->states[i];
  delfromfree(state->freebuf, FREE_SIZE);
  free(state);

  close(cgi_fd);
  FD_CLR(cgi_fd, &p->masterfds);
  p->clientfd[i] = -1;
  log_error(logmsg, logfile);
}


/***************************************************************************/
/* @brief Removes a client and its state from the maintained pool, freeing */
/* up resources and cleaning up memory                                     */
/*                                                                         */
/* @param client_fd  The client fd to be removed from the pool             */
/* @param p          The pool from which to be removed                     */
/* @param logmsg     A msg to write to the logfile                         */
/* @param i          The index into the pool                               */
/***************************************************************************/
void rm_client(int client_fd, pool* p, char* logmsg, int i)
{
  /* Sanitize memory */
  fsm* state = p->states[i];
  if(state->context != NULL) SSL_free(state->context);
  delfromfree(state->freebuf, FREE_SIZE);
  free(state);

  close_socket(client_fd);
  FD_CLR(client_fd, &p->masterfds);
  p->clientfd[i] = -1;
  log_error(logmsg, logfile);
}


/************************************************************/
/* @brief  Writes an error message into the state provided. */
/*                                                          */
/* @param state  The state to write to                      */
/* @param error  The error to write to                      */
/************************************************************/
void client_error(fsm* state, int error)
{
  char* response = state->response;
  char body[LOG_SIZE] = {0};
  char* errnum; char* errormsg;

  memset(response,0,BUF_SIZE);

  switch (error)
  {
    case 404:
      errnum    = "404";
      errormsg  = "Not Found";
      break;
    case 411:
      errnum    = "411";
      errormsg  = "Length Required";
      break;
    case 500:
      errnum    = "500";
      errormsg  = "Internal Server Error";
      break;
    case 501:
      errnum    = "501";
      errormsg  = "Not Implemented";
      break;
    case 503:
      errnum    = "503";
      errormsg  = "Service Unavailable";
      break;
    case 505:
      errnum    = "505";
      errormsg  = "HTTP Version Not Supported";
      break;
    case 400:
      errnum    = "400";
      errormsg  = "Bad Request";
  }

  /* Build the HTTP response body */
  sprintf(response, "HTTP/1.1 %s %s\r\n", errnum, errormsg);
  sprintf(response, "%sContent-type: text/html\r\n", response);
  sprintf(response, "%sServer: Liso/1.0\r\n", response);

  sprintf(body, "<html><title>Webserver Error!</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, errormsg);
  sprintf(body, "%s<hr><em>Fadhil's Web Server </em>\r\n", body);

  sprintf(response, "%sConnection: close\r\n",      response);
  sprintf(response, "%sContent-Length: %d\r\n\r\n", response,(int)strlen(body));

  sprintf(response, "%s%s", response, body);

  state->resp_idx += strlen(response);
}

void cleanup(int sig)
{
  int appease_compiler = sig;
  appease_compiler += 2;

  log_error("Received SIGINT. Goodbye, cruel world.", logfile);
  log_close(logfile);

  fprintf(stderr, "\nThank you for flying Liso. See ya!\n");
  exit(1);
}

void
sigchld_handler(int sig)
{
  pid_t pid; int status;
  int appease_compiler = 0;
  appease_compiler += sig;

  while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0)
  {
    /* Child terminated because of a
     * signal that was not caught. */
    fprintf(stderr, "Child reaped\n");
  }
  return;
}

int daemonize(char* lock_file)
{
        /* drop to having init() as parent */
        int i, lfp, pid = fork();
        char str[256] = {0};
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        setsid();

        for (i = getdtablesize(); i>=0; i--)
                close(i);

        i = open("/dev/null", O_RDWR);
        dup(i); /* stdout */
        dup(i); /* stderr */
        umask(027);

        lfp = open(lock_file, O_RDWR|O_CREAT, 0640);

        if (lfp < 0)
                exit(EXIT_FAILURE); /* can not open */

        if (lockf(lfp, F_TLOCK, 0) < 0)
                exit(EXIT_SUCCESS); /* can not lock */

        /* only first instance continues */
        sprintf(str, "%d\n", getpid());
        write(lfp, str, strlen(str)); /* record pid to lockfile */

        // TODO: log --> "Successfully daemonized lisod process, pid %d."

        return EXIT_SUCCESS;
}
