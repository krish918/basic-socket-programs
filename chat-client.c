/*-------Source code for chat-client.c ---------*/
/*----------------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#define BUFFER_SIZE 1024
#define STDIN 0  /* a constant to hold fd value of stdin which is 0*/

/* a wrapper to handle return values of several socket functions
and displaying appropriate error msg based on returned values. */	
int err_handler(int n, char * error) {
	if(n < 0) {
		perror(error);
		exit(EXIT_FAILURE);
	}
	return n;
};

int main(int argc, char * argv[]) {
	unsigned int PORT = 5050;
	char * ip_address = "127.0.0.1";

	char msg[BUFFER_SIZE];  /* to store the message from sdtin */
	char buffer[BUFFER_SIZE] = {0}; /* To store the received msg from server */

	int client_socket;
	struct sockaddr_in server_address;

	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);

	int recvd_bytes;   /* keeps track of received bytes from server */
	int max_fd, fd_id; /* max_fd is the maximum value of fd which we use 
	in order to have a termination condition for the loop across all FDs.
	fd_id is used as an index for loop. */

	fd_set fd_store, read_fds; /* fd_store stores all the fds and read_fds
	is temporary var which conatins the same set. We don't call 
	select on fd_store as select() calll modifies the fd set. */

	FD_ZERO(&fd_store);  /* we initalise both FD sets */
	FD_ZERO(&read_fds);

	/* Now we translate the ip address to network format */
	err_handler(inet_pton(AF_INET, ip_address, &server_address.sin_addr),
		"IPAddressConversionError");

	/* create the socket */
	client_socket = err_handler(socket(AF_INET, SOCK_STREAM, 0), "SocketError");

	/* Try connecting to the server*/

	err_handler(connect(client_socket, (struct sockaddr_in *) &server_address,
		sizeof(server_address)), "ConnectError");

	printf("\nConnected to server! Escape character is -1.\n");
	printf("\nPress return to start chat.\n");

	FD_SET(STDIN, &fd_store);   /* add 0 to the fd_store, which is the fd for stdin*/
	FD_SET(client_socket, &fd_store);  /* add client_socket to fd_store */
	max_fd = client_socket;

	/* Run a loop indefinitely until it is terminated by user*/
	while(1) {
		/* store fd set temporarily in another empty set */
		read_fds = fd_store;

		/* call select(). First arg is no. of fds, which is one more than max_fd 
		as fd starts from zero. */
		err_handler(select(max_fd+1, &read_fds, NULL, NULL, NULL), "SelectError");

		/* we run a loop across all fds to check which fd is receiving data */
		for(fd_id = 0; fd_id <= max_fd; fd_id++) {
			
			if(FD_ISSET(fd_id, &read_fds)) { /* if fdset denoted by
			fd_id has received something*/

				if(fd_id == client_socket) {  
					/* if we have some data on client_socket we receive 
					it and print it on stdout. */
					bzero(buffer, sizeof(buffer));
					recvd_bytes = recv(fd_id, buffer, sizeof(buffer), 0);

					/* if recvd bytes is 0 then connection is closed by server.
					else if it is -1 then some error ocurred. */
					if(recvd_bytes  <= 0) {
						if(recvd_bytes == 0) {
							printf("\nConnection closed by server.\n");
							exit(EXIT_FAILURE);
						}
						else {
							perror("RecvError");
						}
					}
					else {
						printf("\nServer says: %s", buffer);
						printf("\nYou: ");
						fflush(stdout);
					}
				}
				else if(fd_id == STDIN) {
					/* if data received on fd 0 then client is ending saome data.
					grab it and send to the server.*/
					bzero(msg, sizeof(msg));
					printf("You: ");
					fgets(msg, sizeof(msg), stdin);
					if(!strcmp(msg, "-1\n")) { /* if -1 is entered terminate client*/
						printf("\nbye!\n");
						exit(EXIT_SUCCESS);
					}

					err_handler(send(client_socket, msg, strlen(msg), 0),
						"SendError");					

				}
			}
		}
	}

	close(client_socket);
	return 0;
}