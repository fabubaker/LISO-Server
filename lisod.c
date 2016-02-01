/*************************************************************/
/* @file lisod.c                                             */
/*                                                           */
/* @brief A simple echo-server that uses select() to support */
/* multiple concurrent clients.                              */
/*                                                           */
/* @author Fadhil Abubaker                                   */
/*                                                           */
/* @usage: ./lisod <port>                                    */
/*************************************************************/

/* Part of the code is based on the select-based echo server found in
   CSAPP */

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


#include "logger.h"

typedef struct pool {
  int maxfd;         /* Largest descriptor in the master set */
  fd_set masterfds;  /* Set containing all active descriptors */
  fd_set readfds;    /* Subset of descriptors ready for reading */
  fd_set writefds;   /* Subset of descriptors ready for writing */
  int nready;        /* Number of ready descriptors from select */
  int maxi;          /* Max index of clientfd array             */
  int clientfd[FD_SETSIZE];   /* Array of active client descriptors */
  //fsm* states[FD_SETSIZE]; /* Array of states for each client */
  char data[FD_SETSIZE][BUF_SIZE];   /* Array that contains data from client */
} pool;

/** Global vars **/
FILE* logfile;   /* Legitimate use of globals, I swear! */

/** Prototypes **/

int close_socket(int sock);
void init_pool(int listenfd, pool *p);
void add_client(int client_fd, pool *p);
void check_clients(pool *p);
void cleanup(int sig);

/** Definitions **/

int main(int argc, char* argv[])
{
  if (argc != 4 && argc != 9) // argc = 9
  {
    fprintf(stderr, "%d \n", argc);
    fprintf(stderr, "usage: %s <HTTP port> <HTTPS port> <log file> ", argv[0]);
    fprintf(stderr, "<lock file> <www folder> <CGI script path> ");
    fprintf(stderr, "<privatekey file> <certificate file> \n");
    return EXIT_FAILURE;
  }

  /* Ignore SIGPIPE */
  signal(SIGPIPE, SIG_IGN);
  /* Handle SIGINT to cleanup after liso */
  signal(SIGINT, cleanup);

  /* Parse cmdline args */
  short listen_port = atoi(argv[1]);
  logfile = log_open(argv[3]);
  //  char* wwwfolder = argv[5];

  /* Various buffers for read/write */
  char log_buf[BUF_SIZE] = {0};
  char hostname[BUF_SIZE] = {0};
  char port[BUF_SIZE] = {0};

  int listen_fd, client_fd;    // file descriptors.
  socklen_t cli_size;
  struct sockaddr_in serv_addr, cli_addr;
  pool *pool = malloc(sizeof(struct pool));

  if(pool == NULL)
  {
    log_error("Malloc error! Exiting!", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  fprintf(stdout, "-----Welcome to Liso!-----\n");

  /* all networked programs must create a socket */
  if ((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
  {
    log_error("Failed creating socket.",logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(listen_port);
  serv_addr.sin_addr.s_addr = INADDR_ANY;

  /* Set sockopt so that ports can be resued */
  int enable = -1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) == -1)
  {
    log_error("setsockopt error! Aborting...", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  /* servers bind sockets to ports---notify the OS they accept connections */
  if (bind(listen_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)))
  {
    close_socket(listen_fd);
    log_error("Failed binding socket.", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  if (listen(listen_fd, 5))
  {
    close_socket(listen_fd);
    log_error("Error listening on socket.", logfile);
    log_close(logfile);
    return EXIT_FAILURE;
  }

  /* Initialize our pool of fds */
  init_pool(listen_fd, pool);

  /* finally, loop waiting for input and then write it back */
  while (1)
  {
    /* Block until there are file descriptors ready */
    pool->readfds = pool->masterfds;
    pool->writefds = pool->masterfds;

    if((pool->nready = select(pool->maxfd+1, &pool->readfds, &pool->writefds,
                             NULL, NULL)) == -1)
    {
      close_socket(listen_fd);
      memset(log_buf, 0, BUF_SIZE);
      sprintf(log_buf, "Select failed! Error: %s", strerror(errno));
      log_error(log_buf,logfile);
      log_close(logfile);
      return EXIT_FAILURE;
    }

    /* If the listening descriptor is ready, add the new client to the pool  */
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
                  hostname, BUF_SIZE, port, BUF_SIZE, 0);
      memset(log_buf, 0, BUF_SIZE);
      sprintf(log_buf,
              "We have a new client: Say hi to %s:%s.", hostname, port);
      log_error(log_buf, logfile);
      add_client(client_fd, pool);
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
void init_pool(int listenfd, pool *p)
{
  p->maxi = -1;

  memset(p->clientfd, -1, FD_SETSIZE*sizeof(int)); // No clients at the moment.
  memset(p->data, 0, FD_SETSIZE*BUF_SIZE*sizeof(char)); // No data yet.

  /* Initailly, listenfd is the only member of the read set */
  p->maxfd = listenfd;
  FD_ZERO(&p->masterfds);
  FD_SET(listenfd, &p->masterfds);
}

/*
 * @brief Adds a client file descriptor to the pool and updates it.
 *
 * @param client_fd The client file descriptor.
 * @param p         The pool struct to update.
 */
void add_client(int client_fd, pool *p)
{
  int i;

  p->nready--;

  for (i = 0; i < FD_SETSIZE; i++)  /* Find an available slot */
  {
    if(p->clientfd[i] < 0) {   /* Found one free slot */
      p->clientfd[i] = client_fd;

      /* Add the descriptor to the master set */
      FD_SET(client_fd, &p->masterfds);

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
    log_error("Too many clients! Closing client socket...", logfile);
    close_socket(client_fd);
  }
}

/*
 * @brief Iterates through active clients and reads requests.
 *
 * Uses select to determine whether clients are ready for reading or
 * writing, reads a request. Never blocks for a
 * single user.
 *
 * @param p The pool of clients to iterate through.
 */
void check_clients(pool *p)
{
  int i, client_fd, n;
  char buf[BUF_SIZE];

  memset(buf,0,BUF_SIZE);

  /* Iterate through all clients, and read their data */
  for(i = 0; (i <= p->maxi) && (p->nready > 0); i++)
  {
    client_fd = p->clientfd[i];

    /* If a descriptor is ready to be read, read a line from it */
    if ((client_fd > 0) && (FD_ISSET(client_fd, &p->readfds)))
    {
      p->nready--;

      /* Recv bytes from the client */
      n = recv(client_fd, buf, BUF_SIZE, 0);

      /* We have received bytes, send for parsing. */
      if (n >= 1)
      {
        if (send(client_fd, buf, n, 0) != n)
        {
          close_socket(client_fd);
          fprintf(stderr, "Error sending to client. \n");
        }
        memset(buf,0,BUF_SIZE);
      }

      /* Client sent EOF, close socket. */
      if (n == 0)
      {
        close_socket(client_fd);
        FD_CLR(client_fd, &p->masterfds);
        p->clientfd[i] = -1;
        log_error("Client closed connection with EOF.", logfile);
      }

      /* Error with recv */
      if (n == -1)
      {
        close_socket(client_fd);
        FD_CLR(client_fd, &p->masterfds);
        p->clientfd[i] = -1;
        log_error("Error reading from client socket.", logfile);
      }
    }
  } // End of client loop.
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
