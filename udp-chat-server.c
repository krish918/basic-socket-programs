/*-------Source code for udp-chat-server.c ---------*/
/*----------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 9090
#define BROADCAST_ADDR "192.168.255.255"

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

	int server_sock;

	char sender_ip[INET_ADDRSTRLEN];
	int sender_port;

	struct sockaddr_in sender_addr, recvr_addr;
	socklen_t addr_size = sizeof(struct sockaddr_in);

	memset(&sender_addr, 0, addr_size);
	memset(&recvr_addr, 0, addr_size);

	/*creating socket*/
	server_sock = err_handler(socket(AF_INET, SOCK_DGRAM, 0), "SocketError");

	printf("\nSocket created.\n");

	/*Setting socket address to send broadcast*/
	int yes = 1;
	err_handler(setsockopt(server_sock, SOL_SOCKET, 
		SO_BROADCAST, &yes, sizeof(yes)), "BroadcastSettingError");

	/*We do not need to bind or listen, as we do not establish any prior connection 
	in UDP. We just create socket and start sending out packets 
	by mentioning the receiver address, which we intend to send to.*/

	/*setting up the IP address and port of receiver*/
	recvr_addr.sin_family = AF_INET;
	recvr_addr.sin_addr.s_addr = INADDR_BROADCAST; /* We send to all nodes
	in the network, not to any specific node. */
	recvr_addr.sin_port = htons(PORT);

	while(1) { /*run a loop to keep sending message until user quits*/
		printf("\nEnter a message to send. Enter -1 to exit.\n");
		/*zero out the buffer*/
		memset(buffer, 0, sizeof(buffer));
		fgets(buffer, sizeof(buffer), stdin); /* we take the console 
		input into buffer*/

		if(strcmp(buffer, "-1\n") == 0) {
			printf("\nShutting down server\n");
			break;
		}
		/*we broadcast the content of buffer*/
		err_handler(sendto(server_sock, buffer, strlen(buffer), 0,
			(struct sockaddr_in *) &recvr_addr, addr_size), "SendingError");

		printf("\nWaiting for reply...");
		fflush(stdout);
		err_handler(recvfrom(server_sock, recvd_msg, sizeof(recvd_msg),
		0, (struct sockaddr_in *) &sender_addr, &addr_size),"ReceiveError");
		printf("\nReceived Message: %s\n", recvd_msg);

		/*getting the address and port of sender*/
		if(inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip,
			INET_ADDRSTRLEN) == NULL) {
				perror("AddressTranslationError");
		}
		sender_port = ntohs(sender_addr.sin_port);

		printf("Received from: %s:%d\n", sender_ip, sender_port);
	}

	close(server_sock);

	return 0;
}
