@file   readme.txt
@author Fadhil Abubaker

lisod.c contains source code for a select-based implementation of a web server.

The server is capable of handling a large number of clients with speed and robustness.

The implementation uses a pool to organize clients and client data. A client is added everytime someone connects to the listen port and is then immediately added to the pool. Afterwards, the program iterates through the active clients to read and write data.

The server uses a pool of structs to store the state of each client. The struct acts as a finite state machine, making it easier to implement pipelined requests and to store incomplete messages.

Liso supports HEAD, GET and POST requests. Liso also supports SSL/TLS based communication and can also serve cgi scripts using fork-exec.
