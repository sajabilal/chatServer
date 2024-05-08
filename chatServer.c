#include "chatServer.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <ctype.h>
#define MAX_BUFFER_SIZE 1024
#define MIN_PORT_NUMBER 1
#define MAX_PORT_NUMBER 65535

static int end_server = 0;

void intHandler(int SIG_INT)
{
	//printf("exiting the chat server\n");
	end_server = 1;
}

int main(int argc, char *argv[])
{
	signal(SIGINT, intHandler);
	if (argc != 2)
	{
		printf("Usage: server <port>");
		return (EXIT_FAILURE);
	}
	int port = atoi(argv[1]);
	if (port < MIN_PORT_NUMBER || port > MAX_PORT_NUMBER)
	{
		printf("Usage: server <port>");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGINT, intHandler) == SIG_ERR)
	{
		perror("Unable to set up signal handler");
		exit(EXIT_FAILURE);
	}

	conn_pool_t *pool = malloc(sizeof(conn_pool_t));
	initPool(pool);

	/*************************************************************/
	/* Create an AF_INET stream socket to receive incoming      */
	/* connections on                                            */
	/*************************************************************/

	int server_socket; /* socket descriptor */
	if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	/*************************************************************/
	/* Set socket to be nonblocking. All of the sockets for      */
	/* the incoming connections will also be nonblocking since   */
	/* they will inherit that state from the listening socket.   */
	/*************************************************************/
	int on = 1;
	int rc = ioctl(server_socket, (int)FIONBIO, (char *)&on);
	if (rc == -1)
	{
		perror("ioctl");
		return 1;
	}
	/*************************************************************/
	/* Bind the socket                                           */
	/*************************************************************/
	struct sockaddr_in srv;
	srv.sin_family = AF_INET;
	srv.sin_port = htons(port);
	srv.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (bind(server_socket, (struct sockaddr *)&srv, sizeof(srv)) < 0)
	{
		perror("bind");
		exit(1);
	}
	/*************************************************************/
	/* Set the listen back log                                   */
	/*************************************************************/
	if (listen(server_socket, 5) < 0)
	{
		perror("listen");
		exit(1);
	}
	/*************************************************************/
	/* Initialize fd_sets  			                             */
	/*************************************************************/
	FD_SET(server_socket, &pool->read_set);
	// FD_SET(server_socket, &pool->write_set);
	FD_SET(server_socket, &pool->ready_read_set);
	// FD_SET(server_socket, &pool->ready_write_set);
	pool->maxfd = server_socket;
	/*************************************************************/
	/* Loop waiting for incoming connects, for incoming data or  */
	/* to write data, on any of the connected sockets.           */
	/*************************************************************/
	do
	{
		/**********************************************************/
		/* Copy the master fd_set over to the working fd_set.     */
		/**********************************************************/
		pool->ready_read_set = pool->read_set;
		pool->ready_write_set = pool->write_set;
		/**********************************************************/
		/* Call select()										  */
		/**********************************************************/
		// check which is ready for read
		printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
		int result = select(pool->maxfd + 1, &pool->ready_read_set, &pool->ready_write_set, 0, NULL);
		if (result < 0)
		{
			if (errno == EINTR)
			{
				// Optionally handle the interruption or log it, then continue or retry
				continue; // This will retry the select call
			}
			else
			{
				for (int channel = 0; channel <= pool->maxfd; channel++)
				{
					if ((FD_ISSET(channel, &pool->read_set) && channel != server_socket) || (FD_ISSET(channel, &pool->write_set) && channel != server_socket))
					{
						printf("removing connection with sd %d \n", channel);
						removeConn(channel, pool);
					}
				}
				perror("select");
				exit(1);
			}
		}
		/**********************************************************/
		/* One or more descriptors are readable or writable.      */
		/* Need to determine which ones they are.                 */
		/**********************************************************/

		for (int ffd = 3; ffd < pool->maxfd + 1; ffd++)
		{
			/* Each time a ready descriptor is found, one less has  */
			/* to be looked for.  This is being done so that we     */
			/* can stop looking at the working set once we have     */
			/* found all of the descriptors that were ready         */

			/*******************************************************/
			/* Check to see if this descriptor is ready for read   */
			/*******************************************************/
			if (FD_ISSET(ffd, &pool->ready_read_set))
			{
				/***************************************************/
				/* A descriptor was found that was readable		   */
				/* if this is the listening socket, accept one      */
				/* incoming connection that is queued up on the     */
				/*  listening socket before we loop back and call   */
				/* select again. 						            */
				/****************************************************/
				if (ffd == server_socket)
				{
					int client_socket = accept(ffd, NULL, NULL);
					printf("New incoming connection on sd %d\n", client_socket);
					if (client_socket < 0)
					{
						perror("error: <sys_call>\n");
						exit(1);
					}
					FD_SET(client_socket, &pool->read_set);
					if (client_socket > pool->maxfd)
					{
						pool->maxfd = client_socket;
					}
					pool->nready++;
					addConn(client_socket, pool);
				}

				/****************************************************/
				/* If this is not the listening socket, an 			*/
				/* existing connection must be readable				*/
				/* Receive incoming data his socket             */
				/****************************************************/
				else
				{
					char buffer[MAX_BUFFER_SIZE] = {0};
					ssize_t bytes_read;
					bytes_read = read(ffd, buffer, sizeof(buffer));
					if (bytes_read == 0)
					{
						printf("removing connection with sd %d \n", ffd);
						if (ffd == pool->maxfd){
							for (int i = pool->maxfd-1; i>=0;  i--){
								if (FD_ISSET(i, &pool->read_set)){
									pool->maxfd = i; 
									break;
								}
							}
						}
						removeConn(ffd, pool);
						break;
					}
					printf("Descriptor %d is readable\n", ffd);
					printf("%ld bytes received from sd %d\n", strlen(buffer), ffd);
					buffer[strlen(buffer)] = '\0';
					for (int i = 0; buffer[i] != '\0'; i++)
					{
						buffer[i] = toupper(buffer[i]);
					}
					/* If the connection has been closed by client 		*/
					/* remove the connection (removeConn(...))    		*/

					/**********************************************/
					/* Data was received, add msg to all other    */
					/* connectios					  			  */
					/**********************************************/
					for (int channel = 3; channel < pool->maxfd; channel++)
					{
						FD_SET(channel, &pool->write_set);
					}
					// printf("%s", buffer);
					addMsg(ffd, buffer, strlen(buffer), pool);
				}

			} /* End of if (FD_ISSET()) */
			/*******************************************************/
			/* Check to see if this descriptor is ready for write  */
			/*******************************************************/
			if (FD_ISSET(ffd, &pool->ready_write_set))
			{
				/* try to write all msgs in queue to sd */
				writeToClient(ffd, pool);
				for (int channel = 0; channel < pool->maxfd; channel++)
				{
					if (channel != server_socket && channel != ffd)
					{
						FD_CLR(channel, &pool->write_set);
					}
				}
			}
			/*******************************************************/

		} /* End of loop through selectable descriptors */
	} while (end_server == 0);
	for (int channel = 0; channel <= pool->maxfd; channel++)
	{
		if ((FD_ISSET(channel, &pool->read_set) && channel != server_socket )|| (FD_ISSET(channel, &pool->write_set) && channel != server_socket))
		{
			printf("removing connection with sd %d \n", channel);
			removeConn(channel, pool);
			
		}
	}
	// /*************************************************************/
	// /* If we are here, Control-C was typed,						 */
	// /* clean up all open connections					         */
	// /*************************************************************/
	return 0;
}

int initPool(conn_pool_t *pool)
{
	pool->maxfd = 0;
	pool->nready = 0;
	FD_ZERO(&pool->read_set);
	FD_ZERO(&pool->write_set);
	FD_ZERO(&pool->ready_read_set);
	FD_ZERO(&pool->ready_write_set);
	pool->nr_conns = 0;
	pool->conn_head = NULL;
	return 0;
}

int addConn(int sd, conn_pool_t *pool)
{ /*
   * 1. allocate connection and init fields
   * 2. add connection to pool
   * */
	// new conn_t struct
	conn_t *con = malloc(sizeof(conn_t));
	if (con == NULL)
	{
		perror("Error allocating memory for connection");
		return -1; // Return an error code to indicate failure
	}
	if (pool->conn_head == NULL)
	{
		pool->conn_head = con;
		con->prev = NULL;
		con->next = NULL;
	}
	else
	{
		conn_t *node = pool->conn_head;
		while (node->next != NULL)
		{
			// con->prev = node;
			node = node->next;
		}
		node->next = con;
		con->prev = node;
		con->next = NULL;
	}
	// initiate con
	con->fd = sd;
	con->write_msg_head = NULL;
	con->write_msg_tail = NULL;

	return 0;
}

int removeConn(int sd, conn_pool_t *pool)
{
	if (pool->conn_head == NULL)
	{
		printf("error: no connections to delete\n");
		return 1; // Error code for "nothing to delete"
	}

	conn_t *node = pool->conn_head;
	conn_t *prev = NULL;

	// Find the connection and its previous node in the list
	while (node != NULL && node->fd != sd)
	{
		prev = node;
		node = node->next;
	}

	if (node == NULL)
	{
		printf("error: connection not found\n");
		return 1; // Error code for "not found"
	}

	// If node is not the first node, update the previous node's next pointer
	if (prev != NULL)
	{
		prev->next = node->next;
	}
	else
	{
		// If deleting the first node, update the head
		pool->conn_head = node->next;
	}

	// If node is not the last node, update the next node's prev pointer
	if (node->next != NULL)
	{
		node->next->prev = prev;
	}

	// Free the messages in the write queue
	while (node->write_msg_head != NULL)
	{
		msg_t *tempMsg = node->write_msg_head;
		node->write_msg_head = node->write_msg_head->next;
		free(tempMsg->message); // Don't forget to free the message content itself
		free(tempMsg);
	}

	// Finally, free the node
	free(node);
	node = NULL;
	// Remove fd from sets
	FD_CLR(sd, &pool->read_set);
	FD_CLR(sd, &pool->write_set);
	//close fd
	close(sd);
	// There is no need to update maxfd every time a connection is removed.
	// maxfd only needs to be recalculated when select() reports an error with EBADF.
	// For simplicity and minimal changes, we leave this as is, but it's not efficient.

	return 0;
}

int addMsg(int sd, char *buffer, int len, conn_pool_t *pool)
{
	msg_t *msg = malloc(sizeof(msg_t));
	if (msg == NULL)
	{
		perror("Error allocating memory for connection");
		return -1; // Return an error code to indicate failure
	}
	msg->message = buffer;
	msg->message[strlen(buffer)] = '\0';
	msg->size = len;
	conn_t *node = pool->conn_head;
	while (node != NULL)
	{
		if (node->fd != sd)
		{
			while (node->write_msg_head != NULL)
			{
				node->write_msg_head = node->write_msg_head->next;
			}
			node->write_msg_head = msg;
		}
		node = node->next;
	}

	/*
	 * 1. add msg_t to write queue of all other connections
	 * 2. set each fd to check if ready to write
	 */

	return 0;
}

int writeToClient(int sd, conn_pool_t *pool)
{
	conn_t *node = pool->conn_head;
	while (node != NULL)
	{
		if (node->fd != sd)
		{
			ssize_t bytes_written;
			while (node->write_msg_head != NULL)
			{
				bytes_written = write(node->fd, node->write_msg_head->message, node->write_msg_head->size);
				if (bytes_written == -1)
				{
					// Write operation failed.
					perror("write failed");
					// Handle error, for example, by closing the connection or retrying later.
				}
				node->write_msg_head = node->write_msg_head->next;
			}
		}
		node = node->next;
	}
	/*
	 * 1. write all msgs in queue
	 * 2. deallocate each writen msg
	 * 3. if all msgs were writen successfully, there is nothing else to write to this fd... */

	return 0;
}
