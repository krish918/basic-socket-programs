/*------------Lamport's Logical Clock---------------------------*/
/*------------Naive Implementation with 3 processes--------------*/
/*------------Source code: logical-clock.c----------------------------*/

/*
To run the program, compile with:
gcc -o lamportclock logical-clock.c

Now, first run process B in one window: ./lamportclock -p2
Then, run both process A and C in separate windows: 
./lamportclock -p1 
./lamportclock -p3

This is a naive implementation of lamport's clock, in which Process B receives 
one message each from process A and C. Then, process B sends out one message 
to process A and one message to process C. This is the reason, process B must
be run earlier than process A and C, as process B will be waiting for message
to be received from A and C. 
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 32 
#define PROCESS_A_PORT 5000
#define PROCESS_B_PORT 6000
#define PROCESS_C_PORT 7000

/* defining a macro to get max value out of two values */
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/*
We maintain a structure which contains our message.
The structure also contain the latest logical clock value of the
sending node.
*/

struct message {
	int logical_clock;
	char msg_content[BUFFER_SIZE];
};

/*
A wrapper function to handle all the errors while dealing 
with various networking functions.
*/

int err_handler(int n, char * error) {
	if(n < 0) {
		perror(error);
		exit(EXIT_FAILURE);
	}
	return n;
};

/*
A wrapper around sendto() function, so as to update the logical clock value
before sending a message
*/
int send_message(int sock, struct message * msg, size_t size, int flag, 
	struct sockaddr_in * recvr, socklen_t addrsize) {
	
	/* increment the logical clock value for message send event*/
	msg->logical_clock += 1;

	/*send the UDP packet*/
	return sendto(sock, msg, size, flag, recvr, addrsize);
};

/*
A wrapper around recvfrom() message to update logical clock
*/
int recv_message(int sock, struct message * rcvd_msg, size_t size, struct message * selfmsg) {

	/*
	increment the current logical clock value for message receive event
	*/
	selfmsg->logical_clock += 1;

	/*receive the UDP packet*/
	int recvd_bytes = recvfrom(sock, rcvd_msg, size, 0, NULL, NULL);
	/*we are putting sender address and size of addr as NULL as we don't want to store
	info about sender*/

	/*
	update the current logical clock according to received message  
	*/
	selfmsg->logical_clock = MAX(selfmsg->logical_clock, rcvd_msg->logical_clock + 1);

	return recvd_bytes;
}

void processA(int sock) {
	/*
	Sends out a message to process B.
	Receives a message from process B.
	*/
	
	struct message mesg_a = {0, "A:Hello"}; /*initialising the message*/
	/*Initial vlaue of logical_clock for each process is zero.*/

	printf("\nInitial logical clock value of process A: %d\n", mesg_a.logical_clock);

	struct message recvd_from_b;

	struct sockaddr_in procB_address;
	procB_address.sin_family = AF_INET;
	procB_address.sin_port = htons(PROCESS_B_PORT);
	procB_address.sin_addr.s_addr = INADDR_ANY;

	socklen_t size_addr = sizeof(struct sockaddr_in);

	printf("\nEvent 1 of Process A: snd msg to process B\n");

	/*Sending the message to process B*/
	err_handler(send_message(sock, &mesg_a, sizeof(mesg_a), 0, 
		(struct sockaddr_in *) &procB_address, size_addr), "SendError");

	printf("Logical clock value of Processs A after event 1: %d\n", mesg_a.logical_clock);


	printf("\nEvent 2 of Process A: rcv msg from process B\n");	
	/* Waiting for a message to be received */
	printf("Waiting to receive message...\n");
	err_handler(recv_message(sock, &recvd_from_b, sizeof(struct message), &mesg_a),
		"ReceivingError");

	printf("Logical clock value of Processs A after event 2: %d\n", mesg_a.logical_clock);

};

void processB(int sock) {
	/*
	Receives a message from process A.
	Receives a message from process C.
	Sends a message to process A.
	Sends a message to process C.
	*/

	struct message mesg_b = {0, "B:Hello"}; /*initialising the message*/
	/*Initial vlaue of logical_clock for each process is zero.*/

	printf("\nInitial logical clock value of process B: %d\n", mesg_b.logical_clock);

	struct message recvd_msg;
	struct sockaddr_in recvr_address;

	/*setting up address for any other process running in the same net*/
	recvr_address.sin_family = AF_INET;
	recvr_address.sin_addr.s_addr = INADDR_ANY;

	/*Waiting for the message from process A and C*/
	/* loop runs for 2 times for two time waiting for receive by process B*/
	for(int i = 0; i < 2; i++) {
		printf("\nEvent %d of Process B: recv message\n", i + 1);
		printf("Waiting to receive message...\n");
		err_handler(recv_message(sock, &recvd_msg, sizeof(struct message), &mesg_b),
			"ReceivingError");

		printf("Logical clock value of Process B after event %d: %d\n", 
			i + 1, mesg_b.logical_clock);
	}

	/* Now process B sends message to process A*/
	/* setting port address for process A */
	recvr_address.sin_port = htons(PROCESS_A_PORT);

	printf("\nEvent 3 of Process B: snd msg to process A\n");
	err_handler(send_message(sock, &mesg_b, sizeof(mesg_b), 0, 
		(struct sockaddr_in *) &recvr_address, sizeof(recvr_address)), "SendingError");

	printf("Logical clock value of Processs B after event 3: %d\n", mesg_b.logical_clock);

	/* Now process B sends message to process C*/
	/* setting port address for process C */
	recvr_address.sin_port = htons(PROCESS_C_PORT);

	printf("\nEvent 4 of Process B: snd msg to process C\n");
	err_handler(send_message(sock, &mesg_b, sizeof(mesg_b), 0, 
		(struct sockaddr_in *) &recvr_address, sizeof(recvr_address)), "SendingError");
	printf("Logical clock value of Processs B after event 4: %d\n", mesg_b.logical_clock);

};

void processC(int sock) {
	/*
	Sends out a message to process B.
	Receives a message from process B.
	*/

	struct message mesg_c = {0, "C:Hello"}; /*initialising the message*/
	/*Initial vlaue of logical_clock for each process is zero.*/

	printf("\nInitial logical clock value of process C: %d\n", mesg_c.logical_clock);

	struct message recvd_from_b;

	struct sockaddr_in procB_address;
	procB_address.sin_family = AF_INET;
	procB_address.sin_port = htons(PROCESS_B_PORT);
	procB_address.sin_addr.s_addr = INADDR_ANY;

	socklen_t size_addr = sizeof(struct sockaddr_in);

	printf("\nEvent 1 of Process C: snd msg to process B\n");

	/*Sending the message to process B*/
	err_handler(send_message(sock, &mesg_c, sizeof(mesg_c), 0, 
		(struct sockaddr_in *) &procB_address, size_addr), "SendError");

	printf("Logical clock value of Processs C after event 1: %d\n", mesg_c.logical_clock);


	printf("\nEvent 2 of Process C: rcv msg from process B\n");

	/* Waiting for a message to be received */
	printf("Waiting to receive message...\n");
	err_handler(recv_message(sock, &recvd_from_b, sizeof(struct message), &mesg_c),
		"ReceivingError");

	printf("Logical clock value of Processs C after event 2: %d\n", mesg_c.logical_clock);

}

int main(int argc, char * argv[]) {

	short process_id;

	/* to provide different ports to the 3 processes*/
	int process_port[3] = {PROCESS_A_PORT, PROCESS_B_PORT, PROCESS_C_PORT}; 

	/* the program requires an argument giving process id on comand-line.
	Process id could have any value from set {1, 2, 3}. We are simulating the 
	program on 3 processes. If the argument is not given then instruct and exit. */

	if(argc != 2) {
		printf("\nUsage: ./lamportclock -p<1|2|3>\n\n");
		exit(EXIT_FAILURE);
	}

	int sock_fd; /*file descriptor of socket*/

	int port;  /* contains the assigned port to the current process */

	/* address of the current process.*/
	struct sockaddr_in process_address;
	socklen_t size_of_addr = sizeof(struct sockaddr_in);

	/* zero out process_address */
	memset(&process_address, 0, size_of_addr);

	/*ensure process_id argument is in proper format*/
	if(strlen(argv[1]) != 3) {
		printf("\nUsage: ./lamportclock -p<1|2|3>\n\n");
		exit(EXIT_FAILURE);
	}

	process_id = atoi(argv[1] + 2); /*get 3rd character from the argument as process_id*/

	/* if process_id value is zero, then atoi() could not convert the given 
	argument to a valid integer. */
	if(process_id == 0 || process_id > 3) {
		printf("\nInvalid Argument.");
		exit(EXIT_FAILURE);	
	}

	port = process_port[process_id - 1]; /*selecting one port from the array for current process*/

	/*Create a UDP socket*/
	sock_fd = err_handler(socket(AF_INET, SOCK_DGRAM, 0), "SocketCreationError");
	printf("\nSocket created.\n");

	/*setting up the process address*/
	process_address.sin_family = AF_INET;
	process_address.sin_port = htons(port);
	process_address.sin_addr.s_addr = INADDR_ANY;

	/* We will bind the current process to above set-up address */
	err_handler(bind(sock_fd, (struct sockaddr_in *) &process_address, size_of_addr), "BindError");

	/*
	According to process_id we will invoke respective functions.
	Only one of these functions will run, for any instance of this program.
	*/
	(process_id == 1) ? processA(sock_fd) : 
		((process_id == 2) ? processB(sock_fd) : processC(sock_fd)); 


	return 0;
}