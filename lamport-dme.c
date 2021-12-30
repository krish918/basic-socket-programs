/*---------------LAMPORT'S ALGORITHM for Distributed Mutual Exclusion----------------*/
/*-----------------------------------------------------------------------------------*/
/*********Naive implemetation of lamport's algorithm to solve the mutual exclusion *******
 *********problem, where three processes tried to access the CRITICAL SECTION. The ******* 
 *********CRITICAL SECTION consists of code to write one line in a shared file.***********
 *********All processes must acquire exclusive access to the file before writing to it.***
 * 
 * 
 * We simlulate three processes by invoking the program in three terminals with
 * different arguments. The following source code solves the reader-writer problem for 
 * a shared file, in a very confined environment. For that, several assumptions, of
 * extremly simplistic system of communication, has been taken, (which is not ideal).
 * Hence, the program is written just to demonstrate the basic working of 
 * LAMPORT's ALGORITHM for distributed mutual exclusion and can't be used for real systems. 
 * 
 * 
 * 
 * USAGE:
 * 
 * 1. Compile the program as follows:
 * gcc -o lamport-dme lamport-dme.c
 * 
 * 2. Now, execute the program to run 3 different processes, in 3 DIFFERENT TERMINALS.
 * Execute the processes IN EXACTLY THE FOLLOWING ORDER in different terminals:
 * a) ./lamport-dme -p2
 * b) ./lamport-dme -p3
 * c) ./lamport-dme -p1
 * 
 * 3. Notice the messages on stdout, for all the three processes. All the three processes
 * should finish with last message being  "DONE." 
 * 
 * 4. Next, look for a file named "sharedfile" in the directory which contains
 * the executable code. Open the file to find three lines, each written by three
 * different processes.
 * */ 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 5000
#define FILENAME "sharedfile"
#define REQUEST_SET 2  /* no. of processes from whom, current process needs the permission
to access CS. This is also the maximum no. of processes, which can request the current process
for permission to CS. */

/* constants to define the type of message being sent*/
#define REQUEST_MSG 100
#define REPLY_MSG 200
#define RELEASE_MSG 300

struct message {
	short message_type;
	short message_ts;   /* message timestamp */
	short process_id;
};

struct request_queue {
	short request_ts;   /* request timestamp */
	short requesting_process_id;
	struct request_queue *next_request;  /* A pointer to store next node of request_queue */
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
A utility function  which helps create a socket and set several relevant options
for it and bind it to a port where all broadcast can be received. 
*/
int _create_sock_and_bind(struct sockaddr_in * address, struct sockaddr_in *bcast_address) {
	/* first create the socket required for communication */
	int sock_fd = err_handler(socket(AF_INET, SOCK_DGRAM, 0), "SocketError");

	/*
	setting socket options to reuse a port and to send broadcast
	*/
	int yes = 1;
	err_handler(setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST,
		&yes, sizeof(yes)), "BroadcastSettingError");
	err_handler(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, 
		&yes, sizeof(yes)), "ReuseAddrSettingError");

	socklen_t address_size = sizeof(struct sockaddr_in);

	memset(address, 0, address_size);

	/*setting up ip address */
	address->sin_family = AF_INET;
	address->sin_port = htons(PORT);
	address->sin_addr.s_addr = INADDR_ANY;

	/*process will bind on the above address and also broadcast on it.*/
	err_handler(bind(sock_fd, (struct sockaddr_in *) address, address_size), "BindError");

	printf("\nSocket Created.\n");

	/*
	Setting up the broadcast address on which request and release messages 
	for CS, will be sent
	*/
	bcast_address->sin_family = AF_INET;
	bcast_address->sin_port = htons(PORT);
	bcast_address->sin_addr.s_addr = INADDR_BROADCAST;

	return sock_fd;
}

/*
An utility function to enqueue a new CS request into the request queue of the process
*/
struct request_queue * _put_request_in_request_queue(struct request_queue *queue,
	struct message *msg) {
	
	/* if queue is empty, add at the beginning */
	if(queue == NULL) {
		queue = (struct request_queue *) malloc(sizeof(struct request_queue));
		queue->request_ts = msg->message_ts;
		queue->requesting_process_id = msg->process_id;
		queue->next_request = NULL;
	}
	else {  /* else add the request at the end of queue*/
		struct request_queue * temp = queue;
		while(temp->next_request != NULL) {
			temp = temp->next_request;
		}

		/*at this point, we have reached till the end of queue. 
		now we can add new request*/

		/* create new request node */
		struct request_queue * new_request = (struct request_queue *) 
			malloc(sizeof(struct request_queue));
		new_request->request_ts = msg->message_ts;
		new_request->requesting_process_id = msg->process_id;
		new_request->next_request = NULL;

		/* add the new node to the back of existing queue */
		temp->next_request = new_request;
	}

	printf("Request added to request queue.\n");
	return queue;
};

/*
An utility function to broadcast the critical section request to all other processes
*/
struct request_queue * _send_cs_request(int sock, short pid, struct message *msg, 
	struct request_queue *queue, struct sockaddr_in addr) {

	printf("\nSending request, to write to the shared file, to all other processes\n");

	/* setting up the message content */
	msg->message_type = REQUEST_MSG;
	msg->message_ts += 1;   /* increment the timestamp for sending message */
	msg->process_id = pid;

	/* put the request on the current process request queue in ascending order */
	queue = _put_request_in_request_queue(queue, msg);

	printf("Message timestamp: %d\n", msg->message_ts);

	err_handler(sendto(sock, msg, sizeof(struct message), 0, 
		(struct sockaddr_in *) &addr, sizeof(struct sockaddr_in)), "SendError");

	return queue;
};

/*
an utility function to receive CS request from other processes and
to update the current process ts and request_queue accordingly
*/
struct request_queue * _recv_cs_request(int sock, struct request_queue *queue,
		struct sockaddr_in *addr) {

	printf("\nWaiting for some process's REQUEST to execute CS (write to the shared file)...\n");
	struct message recvd_msg; /* to store the received msg */
	memset(&recvd_msg, 0, sizeof(struct message));

	/* receive the request and store the address of requesting process in *addr 
	as we need to reply back to them later.*/
	socklen_t size_addr = sizeof(struct sockaddr_in);
	err_handler(recvfrom(sock, &recvd_msg, sizeof(struct message), 0, 
		(struct sockaddr_in *) addr, &size_addr), "RecvReqError");

	printf("REQUEST Received.\n");
	printf("Received timestamp: %d\n", recvd_msg.message_ts);

	/* adding the CS request of the recvd message, into the request_queue 
	of current process */
	queue = _put_request_in_request_queue(queue, &recvd_msg);
	return queue;
};

/*
an utility function to receive reply from other processes regarding the 
permission to CS i.e. writing to the shared file.
*/
void _recv_reply(int sock, short pid, struct message *msg, short *perm) {

	struct message recvd_msg;

	/* receive the reply message and make sure, only appropriate message is received.
	The reply should be ignored by receivers, which are not expecting a reply message.*/
	while(1) {
		err_handler(recvfrom(sock, &recvd_msg, sizeof(struct message),
			0, NULL, NULL), "ReceiveError");
		if(recvd_msg.message_type == REPLY_MSG && recvd_msg.process_id != pid) {
			break;
		}
	}

	printf("Received timestamp: %d\n", recvd_msg.message_ts);
	/* checking whether ts of received msg is greater than current process */
	if(recvd_msg.message_ts > msg->message_ts) {

		/* the replying process is providing the permission to CS */
		perm[recvd_msg.process_id - 1] = 1;
	}
};

int checkLamportCondition(struct request_queue *front, short perm[], short pid) {
	/* 
	checking the first lamport's condition L1 for distibuted mutual exclusion
	*/
	printf("\nChecking lamport's condition for distributed mutual exclusion.\n");

	/* all the timestamped reply to current process request must be larger than 
	current process ts i.e. all indexes of permission array must have value 1.*/
	for(int i = 0; i < REQUEST_SET + 1; i++) {
		if(perm[i] == 0) {
			printf("process %d denied permission.\n", i+1);
			/* it means at least one permission from some process is missing*/
			return 0;
		}
	}

	/* 
	checking the 2nd lamport's condition L2 for distributed mutex 
	*/ 

	/* front pointer points at the front of the queue. If current process request 
	is not at the front of request queue then, the processs cannot execute CS*/
	if(front->requesting_process_id != pid) {
		printf("Process %d request is not at the front of request queue.\n", pid);
		return 0;
	}

	printf("Conditions statisfied.\n");
	printf("All processes have granted permission to process %d.\n", pid);
	return 1;
};

/*
A function where critical section is executed i.e. a shared file is opened
and is written by a process in isolated way.
*/
void startWritingToFile(short pid) {
	printf("\nEntering Critical Section. Writing to shared file...\n");
	FILE * shared_file = fopen(FILENAME, "a");
	int written;

	if(shared_file == NULL) {
		perror("FileOpenError");
		exit(EXIT_FAILURE);
	}

	/* now the process writes to the file and closes the file*/
	char line_to_write[40];
	sprintf(line_to_write, "This an entry written by process %d\n", pid);
	written = fputs(line_to_write, shared_file);

	if(written > 0) {
		printf("\nProcess %d wrote %d bytes to the file.\n", pid, strlen(line_to_write));
	}
	else {
		perror("WriteError");
		exit(EXIT_FAILURE);
	}

	fclose(shared_file);
}

/*
send the timestamped reply to the process requsting for CS
*/
void _send_reply(int sock, short pid, struct message * msg, 
	struct sockaddr_in requester_address) {

	printf("\nSending timestamped reply to an outstanding file write request.\n");

	/* increment the timestamp of reply  message*/
	msg->message_ts += 1;
	msg->message_type = REPLY_MSG;

	printf("Message timestamp: %d\n", msg->message_ts);

	err_handler(sendto(sock, msg, sizeof(struct message), 0, 
		(struct sockaddr_in *) &requester_address, 
		sizeof(struct sockaddr_in)), "SendReplyError");
}

/*
A function to remove own request from a process own request queue,
and send release message to all other processes
*/
struct request_queue * _send_release(int sock, struct message * msg, 
	struct request_queue * front, struct sockaddr_in addr) {

	printf("\nExecution of critical section done.");
	printf("\nSending release message to all other processes.\n");

	/* incrementing the message timestamp  */
	msg->message_ts += 1;
	msg->message_type = RELEASE_MSG;

	printf("Message timestamp: %d\n", msg->message_ts);

	/* remove the current process request from front of the queue */
	if(front != NULL) {
		struct request_queue * temp = front;
		front = front->next_request;
		free(temp);
		temp = NULL;
	}

	/* broadcast the timestamped release message */
	err_handler(sendto(sock, msg, sizeof(struct message), 0, 
		(struct sockaddr_in *) &addr, sizeof(struct sockaddr_in)), "SendReleaseError");

	return front;
}

/*
receiving release message from some other process and removing that process's request
from the request queue
*/
struct request_queue * _recv_release(int sock, short pid, struct request_queue *req) {

	printf("\nWaiting for RELEASE message from the process writing to file.\n");
	struct message recvd_msg;

	/* receive the release message and make sure, it is a message being expected.
	As the release is broadcasted, it should be ignored by receivers, which are
	not expecting this message.*/
	while(1) {
		err_handler(recvfrom(sock, &recvd_msg, sizeof(struct message),
			0, NULL, NULL), "RecvRlsError");
		if(recvd_msg.message_type == RELEASE_MSG && recvd_msg.process_id != pid) {
			break;
		}
	}

	short requester_id = recvd_msg.process_id;

	/* looking for the request in the request queue of current process and deleting it*/
	struct request_queue * temp = req;
	struct request_queue *temp2 = NULL;
	if(temp != NULL) {
		if(temp->requesting_process_id == requester_id) {
			req = temp->next_request;
			free(temp);
			temp = NULL;
		}
		else {
			while(temp->next_request != NULL) {
				if(temp->next_request->requesting_process_id == requester_id) {	
					temp2 = temp->next_request;
					temp->next_request = temp->next_request->next_request;
					free(temp2);
					temp2 = NULL;
					break;
				}

				temp = temp->next_request;
			}
		}
	}

	printf("RELEASE Message Received.\n");
	return req; 
};



/*
Code corresponding to process 1. It sends the first REQUEST to write to a shared file,
and then receives two REQUESTs to write to same file, consecutively, from other two 
processes. Then, it receives the REPLY of other two processes regarding the file writing request. 
After that, it checks it's request queue and executes the critical section and finally
sends out the RELEASE message to all other processes.
*/
void _exec_process_1() {
	short process_id = 1;

	short permission[REQUEST_SET+1] = {0}; /* an array to store permissions from other processes*/

	/* permission to execute CS from itself is always granted */
	permission[process_id - 1] = 1; 

	/*
	we declare a sockaddr_in array of size equal to REQUEST_SET, as these
	many processes can request the current process for CS permission.
	current process need to have their addresses, in order to reply to them.
	*/
	struct sockaddr_in requester_address[REQUEST_SET]; 

	/* address to which current process will bind to */
	struct sockaddr_in address, bcast_address;
	int sock = _create_sock_and_bind(&address, &bcast_address);

	/*
	socket is created and binded. Now process 1 broadcasts its request to write to the file. 

	we create the REQUEST message to be sent by process 1
	*/
	struct message msg_process_1 = {0}; /* initiate all values to zero */

	/* initiate the request queue and keep a pointer to the front of queue*/ 
	struct request_queue * front = NULL;

	/*Setting all addresses corresponding to requesting processes as zero*/
	for(int i = 0; i < REQUEST_SET; i++) {
		memset(&requester_address[i], 0, sizeof(requester_address[i]));
	}

	/* send the request to execute critical section to all processes */
	front = _send_cs_request(sock, process_id, &msg_process_1, front, bcast_address);

	/* 
	after sending its own CS request, process 1 receives the CS request from 
	process 2 and 3. It receives them and stores in request_queue
	*/
	for(int i = 0; i < REQUEST_SET; i++) {
		front = _recv_cs_request(sock, front, &requester_address[i]);
	}

	/* Till now, the process has sent its own CS request and received CS requests of all other
	processes. Now, it will wait for reply from all other processes, in order to enter the CS.
	The CS or critical section here is the shared file which every process wants to write. */

	/* getting the replies */
	printf("\nWaiting for REPLY from all other processes.\n");
	for(int i = 0; i < REQUEST_SET; i++) {
		_recv_reply(sock, process_id, &msg_process_1, permission);
		printf("Received REPLY %d\n", i+1);
	}

	/*If the process's REQUEST timestamp is smaller than the received timestamp,
	hopefully we will enter the critical section.*/
	printf("\nProcess %d REQUEST timestamp: %d\n", process_id, msg_process_1.message_ts);

	/* 
	after receiving the reply we check two conditions L1 and L2 of lamport's algorithm 
	and if the condition is satisfied, process 1 exceutes CS
	*/
	short eligible_for_cs = checkLamportCondition(front, permission, process_id);

	if(eligible_for_cs == 1) {
		startWritingToFile(process_id);
	}

	/* now the process will tell everyone that it's releasing the critical section */
	front = _send_release(sock, &msg_process_1, front, bcast_address);

	/* Now, process 1 has outstanding CS requests from all other processes
	in the request set. It will now send a timestamped reply to these processes one by
	one*/

	for(int i = 0; i < REQUEST_SET; i++) {
		_send_reply(sock, process_id, &msg_process_1, bcast_address);

		/* after sending the reply, we wait for the release message */
		front = _recv_release(sock, process_id, front);
	}

	printf("\nDONE.\n");
};


/*
Code corresponding to process 2. Similar to _exec_process_1(), however, the ordering of
sendto() and recvfrom() will be different.
*/
void _exec_process_2() {
	short process_id = 2;
	int bytes_written;

	short permission[REQUEST_SET+1] = {0}; /* an array to store permissions from other processes*/

	/* permission to execute CS from itself is always granted */
	permission[process_id - 1] = 1; 

	/*
	we declare a sockaddr_in array of size equal to REQUEST_SET, as these
	many processes can request the current process for CS permission.
	current process need to have their addresses, in order to reply to them.
	*/
	struct sockaddr_in requester_address[REQUEST_SET]; 

	/* address to which current process will bind to */
	struct sockaddr_in address, bcast_address;
	int sock = _create_sock_and_bind(&address, &bcast_address);

	/*
	socket is created and binded. Now process 1 broadcasts its request to write to the file. 

	we create the REQUEST message to be sent by process 1
	*/
	struct message msg_process_2 = {0}; /* initiate all values to zero */

	/* initiate the request queue and keep a pointer to the front of queue*/ 
	struct request_queue * front = NULL; 

	/*Setting all addresses corresponding to requesting processes as zero*/
	for(int i = 0; i < REQUEST_SET; i++) {
		memset(&requester_address[i], 0, sizeof(requester_address[i]));
	}

	front = _recv_cs_request(sock, front, &requester_address[0]);
	
	/* send the request to execute critical section to all processes */
	front = _send_cs_request(sock, process_id, &msg_process_2, front, bcast_address);

	front = _recv_cs_request(sock, front, &requester_address[1]);

	_send_reply(sock, process_id, &msg_process_2, bcast_address);

	front = _recv_release(sock, process_id, front);

	printf("\nWaiting for REPLY from all other processes.\n");
	for(int i = 0; i < REQUEST_SET; i++) {
		_recv_reply(sock, process_id, &msg_process_2, permission);
		printf("Received REPLY %d\n", i+1);
	}

	/*If the process's REQUEST timestamp is smaller than the received timestamp,
	hopefully we will enter the critical section.*/
	printf("\nProcess %d REQUEST timestamp: %d\n", process_id, msg_process_2.message_ts);

	short eligible_for_cs = checkLamportCondition(front, permission, process_id);

	if(eligible_for_cs == 1) {
		startWritingToFile(process_id);
	}

	/* now the process will tell everyone that it's releasing the critical section */
	front = _send_release(sock, &msg_process_2, front, bcast_address);

	_send_reply(sock, process_id, &msg_process_2, bcast_address);

	front = _recv_release(sock, process_id, front);

	printf("\nDONE.\n");
};

void _exec_process_3() {
	short process_id = 3;
	int bytes_written;

	short permission[REQUEST_SET+1] = {0}; /* an array to store permissions from other processes*/

	/* permission to execute CS from itself, is always granted */
	permission[process_id - 1] = 1; 

	/*
	we declare a sockaddr_in array of size equal to REQUEST_SET, as these
	many processes can request the current process for CS permission.
	current process need to have their addresses, in order to reply to them.
	*/
	struct sockaddr_in requester_address[REQUEST_SET]; 

	/* address to which current process will bind to */
	struct sockaddr_in address, bcast_address;
	int sock = _create_sock_and_bind(&address, &bcast_address);

	/*
	socket is created and binded. Now process 1 broadcasts its request to write to the file. 

	we create the REQUEST message to be sent by process 1
	*/
	struct message msg_process_3 = {0}; /* initiate all values to zero */

	/* initiate the request queue and keep a pointer to the front of queue*/ 
	struct request_queue * front = NULL; 

	/*Setting all addresses corresponding to requesting processes as zero*/
	for(int i = 0; i < REQUEST_SET; i++) {
		memset(&requester_address[i], 0, sizeof(requester_address[i]));
	}

	for(int i = 0; i < REQUEST_SET; i++) {
		front = _recv_cs_request(sock, front, &requester_address[i]);
	}

	front = _send_cs_request(sock, process_id, &msg_process_3, front, bcast_address);

	for(int i = 0; i < REQUEST_SET; i++) {
		_send_reply(sock, process_id, &msg_process_3, bcast_address);

		/* after sending the reply, we wait for the release message */
		front = _recv_release(sock, process_id, front);
	}

	printf("\nWaiting for REPLY from all other processes.\n");
	for(int i = 0; i < REQUEST_SET; i++) {
		_recv_reply(sock, process_id, &msg_process_3, permission);
		printf("Received REPLY %d\n", i+1);
	}

	/*If the process's REQUEST timestamp is smaller than the received timestamp,
	hopefully we will enter the critical section.*/
	printf("\nProcess %d REQUEST timestamp: %d\n", process_id, msg_process_3.message_ts);

	short eligible_for_cs = checkLamportCondition(front, permission, process_id);

	if(eligible_for_cs == 1) {
		startWritingToFile(process_id);
	}

	/* now the process will tell everyone that it's releasing the critical section */
	front = _send_release(sock, &msg_process_3, front, bcast_address);

	printf("\nDONE.\n");
};

int main(int argc, char * argv[]) {
	short process_id;

	if(argc != 2) {
		if(argc < 2) {
			printf("\nToo few arguments.\n");
		}
		else {
			printf("\nToo many arguments.\n");	
		}
		printf("\nUsage: ./lamport-dme -p<1|2|3>\n\n");
		exit(EXIT_FAILURE);
	}

	/*ensure process_id argument is in proper format*/
	if(strlen(argv[1]) != 3) {
		printf("\nInvalid Argument.\n");
		printf("\nUsage: ./lamport-dme -p<1|2|3>\n\n");
		exit(EXIT_FAILURE);
	}

	process_id = atoi(argv[1] + 2); /*get 3rd character from the argument as process_id*/

	/* if process_id value is zero, then atoi() could not convert the given 
	argument to a valid integer. */
	if(process_id == 0 || process_id > 3) {
		printf("\nInvalid Argument.\n");
		exit(EXIT_FAILURE);	
	}

	/*
	we simulate the idea of three processes by invoking three functions in three 
	different executions of same program in different terminals.
	*/

	if(process_id == 1) {
		_exec_process_1();
	}
	else if(process_id == 2) {
		_exec_process_2();
	}
	else {
		_exec_process_3();
	}

	return 0;
}