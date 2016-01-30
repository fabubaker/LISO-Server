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

#define BUF_SIZE 4096

typedef struct pool {
  int maxfd;         /* Largest descriptor in the master set */
  fd_set masterfds;  /* Set containing all active descriptors */
  fd_set readfds;    /* Subset of descriptors ready for reading */
  fd_set writefds;   /* Subset of descriptors ready for writing */
  int nready;        /* Number of ready descriptors from select */
  int maxi;          /* Max index of clientfd array             */
  int clientfd[FD_SETSIZE];   /* Array of active client descriptors */
  int stored[FD_SETSIZE];/* Array of bools used for data storage indication */
  char data[FD_SETSIZE][BUF_SIZE];   /* Array that contains echo strings */
} pool;


/** Prototypes **/

int close_socket(int sock);
void init_pool(int listenfd, pool *p);
void add_client(int client_fd, pool *p);
void check_clients(pool *p);

/** Definitions **/

int main(int argc, char* argv[])
{
  if (argc != 2 && argc != 9) // argc = 9
  {
    fprintf(stderr, "%d \n", argc);
    fprintf(stderr, "usage: %s <HTTP port> <HTTPS port> <log file> ", argv[0]);
    fprintf(stderr, "<lock file> <www folder> <CGI script path> ");
    fprintf(stderr, "<privatekey file> <certificate file> \n");
    return EXIT_FAILURE;
  }

  /* Ignore SIGPIPES */
  signal(SIGPIPE, SIG_IGN);

  short listen_port = atoi(argv[1]);
  int listen_fd, client_fd;    // file descriptors.
  socklen_t cli_size;
  struct sockaddr_in serv_addr, cli_addr;
  pool *pool = malloc(sizeof(struct pool));

  fprintf(stdout, "----- Echo Server -----\n");

  /* all networked programs must create a socket */
  if ((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
  {
    fprintf(stderr, "Failed creating socket.\n");
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
    fprintf(stderr,"setsockopt error! Aborting...\n");
    return EXIT_FAILURE;
  }

  /* servers bind sockets to ports---notify the OS they accept connections */
  if (bind(listen_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)))
  {
    close_socket(listen_fd);
    fprintf(stderr, "Failed binding socket.\n");
    return EXIT_FAILURE;
  }

  if (listen(listen_fd, 5))
  {
    close_socket(listen_fd);
    fprintf(stderr, "Error listening on socket.\n");
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
    // fprintf(stderr, "Waiting for select...\n");
    if((pool->nready = select(pool->maxfd+1, &pool->readfds, &pool->writefds,
                             NULL, NULL)) == -1)
    {
      close_socket(listen_fd);
      fprintf(stderr, "Error: %s \n", strerror(errno));
      fprintf(stderr, "Select() failed! Aborting...\n");
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
        fprintf(stderr, "Error accepting connection.\n");
        return EXIT_FAILURE;
      }
      fprintf(stderr,"We have a new client! \n");
      add_client(client_fd, pool);
    }

    /* Echo a text line from each ready descriptor */
    check_clients(pool);
  }
}

int close_socket(int sock)
{
  if (close(sock))
  {
    fprintf(stderr, "Failed closing socket.\n");
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
  memset(p->stored, -1, FD_SETSIZE*sizeof(int));
  //  for(i = 0; i < FD_SETSIZE; i++)
  //p->clientfd[i] = -1;

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
  //  bool found = false;

  p->nready--;

  // fprintf(stderr, "FD_SETSIZE = %d \n", FD_SETSIZE);
  fprintf(stderr,"Clients before:: %d\n", p->maxi);

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

  fprintf(stderr,"Clients after:: %d\n", p->maxi);

  if (i == FD_SETSIZE)   /* There are no empty slots */
  {
    fprintf(stderr,"Too many clients! Closing client socket...\n");
    close_socket(client_fd);
  }
}

/*
 * @brief Iterates through active clients and echoes a text line.
 *
 * Uses select to determine whether clients are ready for reading or
 * writing, then echoes a single line of text. Never blocks for a
 * single user.
 *
 * @param p The pool of clients to iterate through.
 */
void check_clients(pool *p)
{
  int i, client_fd, n;
  char buf[BUF_SIZE];

  memset(buf,0,BUF_SIZE);

  for(i = 0; (i <= p->maxi) && (p->nready > 0); i++)
  {
    client_fd = p->clientfd[i];

    /* If a descriptor is ready to be read, echo a text line from it */
    if ((client_fd > 0) && (FD_ISSET(client_fd, &p->readfds)))
    {
      p->nready--;

      // fprintf(stderr, "Reading data...\n");
      if ((n = recv(client_fd, buf, BUF_SIZE, 0)) >= 1)
      {
        // fprintf(stderr,"RECEIVED: %s \n", buf);
        // fprintf(stderr, "Sending data...\n");
        /* Check if ready for writing */
        if(FD_ISSET(client_fd, &p->writefds))
        {
          // fprintf(stderr,"SENDING: %s \n", buf);
          if (send(client_fd, buf, n, 0) != n)
          {
            close_socket(client_fd);
            fprintf(stderr, "Error sending to client. \n");
          }
          // fprintf(stderr, "Data sent!...\n");
          memset(buf,0,BUF_SIZE);
        }
        /* Not ready, save request. */
        else
        {
          strncpy(p->data[i], buf, n);
          p->stored[i] = n;
          fprintf(stderr, "Data stored. \n");
          memset(buf,0,BUF_SIZE);
        }
      }

      if (n == 0) /* Client sent EOF, close socket. */
      {
        close_socket(client_fd);
        FD_CLR(client_fd, &p->masterfds);
        p->clientfd[i] = -1;
        p->stored[i] = -1;
        fprintf(stderr, "Client closed connection with EOF. \n");
      }

      if (n == -1) /* Error with recv */
      {
        close_socket(client_fd);
        FD_CLR(client_fd, &p->masterfds);
        p->clientfd[i] = -1;
        p->stored[i] = -1;
        fprintf(stderr,"Error reading from client socket. \n");
      }
    }
    /* If a descriptor has data to be sent, send it */
    else if ((client_fd > 0) && (FD_ISSET(client_fd, &p->writefds)) &&
             p->stored[i] > 0)
    {
      p->nready--;

      if (send(client_fd, p->data[i], n, 0) != n)
      {
        close_socket(client_fd);
        fprintf(stderr, "Error sending to client. \n");
      }
      // fprintf(stderr, "Data sent!...\n");
    }
  } // End of client loop.
}
