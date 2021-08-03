/*-------Source code for udp-chat-client.c ---------*/
/*----------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define PORT 9090

int err_handler(int n, char * error) {
	if(n < 0) {
		perror(error);
		exit(EXIT_FAILURE);
	}
	return n;
};

int main(int argc, char * argv[]) {
	char buffer[BUFFER_SIZE];
	char recvd_msg[BUFFER_SIZE];

	char sender_ip[INET_ADDRSTRLEN];
	int sender_port;

	int sock_fd;

	struct sockaddr_in sender_addr, recvr_addr;
	socklen_t addr_size = sizeof(struct sockaddr_in);

	memset(&sender_addr, 0, addr_size);
	memset(&recvr_addr, 0, addr_size);

	/*creating socket*/
	sock_fd = err_handler(socket(AF_INET, SOCK_DGRAM, 0), "SocketError");

	/*setting up ip address */
	recvr_addr.sin_family = AF_INET;
	recvr_addr.sin_port = htons(PORT);
	recvr_addr.sin_addr.s_addr = INADDR_ANY;

	/*Setting socket address to send broadcast*/
	int yes = 1;
	err_handler(setsockopt(sock_fd, SOL_SOCKET, 
		SO_REUSEADDR, &yes, sizeof(yes)), "REUSEADDRSettingError");

	/*we will now bind this socket to PORT and INADDR_ANY. This is 
	because we need to recv some data, and for that, we need to be available
	on some port, as the data sent is designated by IP address & port no.*/

	err_handler(bind(sock_fd, (struct sockaddr_in *) &recvr_addr,
		addr_size),"BindError");

	printf("\nClient binded. Waiting for message...\n", PORT);

	err_handler(recvfrom(sock_fd, recvd_msg, sizeof(recvd_msg),
		0, (struct sockaddr_in *) &sender_addr, &addr_size),"ReceiveError");

	printf("\nReceived Msg: %s", recvd_msg);

	/*getting the address and port of sender*/
	if(inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip,
		INET_ADDRSTRLEN) == NULL) {
		perror("AddressTranslationError");
	}
	sender_port = ntohs(sender_addr.sin_port);
	printf("Received from: %s:%d\n", sender_ip, sender_port);

	printf("\nEnter a message to reply to the sender. Enter -1 to quit.\n");
	fgets(buffer, sizeof(buffer), stdin);
	if(strcmp(buffer, "-1\n") == 0) {
		exit(EXIT_SUCCESS);
	}

	err_handler(sendto(sock_fd, buffer, sizeof(buffer), 0, 
		(struct sockaddr_in *) &sender_addr, addr_size), "SendingError");

	close(sock_fd);

	return 0;
};