/*-------Source code for chat-server.c ---------*/
/*----------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#define BUFFER_SIZE 1024
#define BACKLOG 10  /* Maximum no. of outstanding client request after which
	  any connection request will be refused. */
#define STDIN 0 /* a constant to hold fd value of stdin which is 0*/

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
	// Server will listen on port 5050
	unsigned int PORT = 5050;
	
	// File descriptors for server socket and socket which identifies
	//   the connected client.
	int server_socket, client_conn;
	// Structure which keeps attributes of the address to be binded with
	struct sockaddr_in server_address, client_address;
	int size_address = sizeof(server_address);
	//zeroing out the both address structures
	memset(&server_address, 0, size_address);
	memset(&client_address, 0, sizeof(client_address));
	
	//message to be sent from server
	char server_msg[BUFFER_SIZE]= {0};
	char buffer[BUFFER_SIZE] = {0}; /*message received by client*/

	//After accepting connection from a client, we want to know 
	//  its ip address and port.
	char client_ip[INET_ADDRSTRLEN];
	unsigned int client_port;
	int recvd_bytes; /* keeps track of received bytes from server */
	int max_fd, fd_id; /* max_fd is the maximum value of fd which we use 
	in order to have a termination condition for the loop across all FDs.
	fd_id is used as an index for loop. */
	
	fd_set fd_store, read_fds; /* fd_store stores all the fds and read_fds
	is temporary var which conatins the same set. We don't call 
	select on fd_store as select() calll modifies the fd set. */

	FD_ZERO(&fd_store); /* we initalise both FD sets */
	FD_ZERO(&read_fds);

	//creating the socket used by server
	server_socket = err_handler(socket(AF_INET, SOCK_STREAM, 0), "SocketError");

	printf("\nServer Socket created.\n");

	//populating various attributes of address the server will use
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(PORT);

	//setting socket options for forcefully binding
	int yes = 1;
	err_handler(setsockopt(server_socket, SOL_SOCKET, 
		SO_REUSEADDR, &yes, sizeof(yes)), "SetsockoptError");

	// binding the server to the said address and port
	err_handler(bind(server_socket, 
		(struct sockaddr_in *) &server_address, size_address), "BindError");

	printf("\nServer binded to port %d.\n", PORT);

	// listening starts for a connection
	err_handler(listen(server_socket, BACKLOG), "ListenError");

	printf("\nServer is listening...\n");

	FD_SET(STDIN, &fd_store); /* add 0 to the fd_store, which is the fd for stdin*/
	FD_SET(server_socket, &fd_store);  /* add client_socket to fd_store */
	max_fd = server_socket;

	/*We run a loop to accept a connection whenever it is available. This way
	server will not exit once the connection by client is closed. */
	while(1) {
		/* store fd set temporarily in another empty set */
		read_fds = fd_store;

		/* call select(). First arg is no. of fds, which is one more than max_fd 
		as fd starts from zero. */
		err_handler(select(max_fd+1, &read_fds, NULL, NULL, NULL), "Select");

		/* we run a loop across all fds to check which fd is receiving data */
		for(fd_id = 0; fd_id <= max_fd; fd_id++) {
			
			if(FD_ISSET(fd_id, &read_fds)) { /* if fdset denoted by
			fd_id has received something*/
			
				if(fd_id == server_socket) {
					/* if we have some data on server_socket then we are
					getting anew connection from a client. */
					client_conn = accept(server_socket, 
						(struct sockaddr_in *) &client_address, 
						(socklen_t *) &size_address);  /* accept the new connection */

					if(client_conn == -1) {
						perror("ConnectionAcceptError");
					}
					else {
						/*add the new socket returned by the accept()
						call, (which corresponds to new client) 
						into the fd set*/
						FD_SET(client_conn, &fd_store);
						
						/*update the max_fd value*/
						if(client_conn > max_fd) {
							max_fd = client_conn;
						}
						
						/* get the ipaddress and port of the new client and
						display it*/
						if(inet_ntop(AF_INET, &client_address.sin_addr,
	 						client_ip, INET_ADDRSTRLEN) == NULL) {
	 							perror("Address transalation");
						}

						client_port = ntohs(client_address.sin_port);
						printf("\nServer is connected to: %s at port %u\n", 
							client_ip, client_port);

						printf("\nPress return to start chat.\n");
					}
				}
				else if(fd_id == STDIN) {
					/* if data received on fd 0 then server is trying 
					to send some data to client.
					get the data fro stdin into server_msg variable*/
					bzero(server_msg, sizeof(server_msg));
					printf("You: ");
					fgets(server_msg, sizeof(server_msg), stdin);

					/* if -1 is entered on stdin the terminate server */
					if(strcmp(server_msg, "-1\n") == 0) {
						printf("\nGot kill signal.Shutting Down Server.\n");
						exit(EXIT_SUCCESS);
					}
					
					/* loop through all FDs*/
					for(int send_id = 0; send_id <= max_fd; send_id++) {
						/*if the FD is set in the FD*/
						if(FD_ISSET(send_id, &fd_store)) {

							/* and if it is not server socket nor a stdin 
							i.e. it only belongs to all connected clients.*/
							if(send_id != STDIN && send_id != server_socket) {

								/* then broadcast the meesage to all clients.*/
								err_handler(send(send_id, server_msg, 
									strlen(server_msg), 0), "SendError");
																fflush(stdout);
							}
						}
					}
				}
				else {
					/* if the fd on which data is received is neither 
					stdin nor the server socket, then it means data from
					some client has been received.*/
					bzero(buffer, sizeof(buffer));

					/* recv the dat into buffer*/
					recvd_bytes = recv(fd_id, buffer, sizeof(buffer), 0);

					/*check for errors*/
					if(recvd_bytes <= 0) {
						if(recvd_bytes == 0) {
							printf("\nConnection closed by socket %d.", fd_id);
							fflush(stdout);
						}
						else {
							perror("RecvError");
						}

						close(fd_id);
						FD_CLR(fd_id, &fd_store);
					}
					else {
						/* print the msg received by the client */
						printf("\nClient %d says: %s",fd_id, buffer);
						printf("\nYou: ");
						fflush(stdout);
					}
				}
			}
		}
	}

	close(server_socket);
	return 0;
}