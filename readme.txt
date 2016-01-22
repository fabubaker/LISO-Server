@file   readme.txt
@author Fadhil Abubaker

lisod.c contains source code for a select-based implementation of an
echo server.

The server is capable of handling a large number of clients with speed and robustness.

The implementation uses a pool to organize clients and client data. A client is added everytime someone connects to the listen port and is then immediately added to the pool. Afterwards, the program iterates through the active clients to read and write data.
