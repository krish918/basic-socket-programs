#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#define PORT 6060
#define BUFFER_SIZE 1400
#define FILENAME_SIZE 72
#define BACKLOG 3
#define TIMEOUT_OCCURED -2  /* a constant to signal timeout has ocurred */
#define FILESIZE_STRING 11 /* We want atmost 9,999,999,999 Bytes of file 9.9GB*/
#define LOGFILE_NAME "server_log"
#define RECEIVED_LOG "temp"
#define FILE_RECORD_LINE_NUMBER 6 /*as we are writing both, plain text and 
structure to the same log file. Hence we need to store the line number from where
structure entry is starting.*/

/* These constants will be used as flag to decide what data in the logfile
has to be udated corresponding to a file*/
#define UPDATE_LOG_PROGRESS 0 
#define UPDATE_LOG_COMPLETED 1
#define UPDATE_LOG_TIMEOUT 2
#define UPDATE_LOG_CONNECTION_COUNT 3

#define FRESH_UPLOAD 100
#define REATTEMPT_UPLOAD 50

struct segment {
	int seq_no;
	int ack_no;
	char filename[FILENAME_SIZE];
	char filesize[FILESIZE_STRING];
	char buffer[BUFFER_SIZE];
};
typedef struct log {
	char filename[FILENAME_SIZE];
	char filesize[FILESIZE_STRING];
	char start_time[18];
	char end_time[18];
	unsigned long bytes_transferred;
	unsigned short percentage_completion;
	unsigned int connection_count;
	unsigned int timeout_count;
} server_log;

char * _get_current_date_time() {
	char date_time[18];

	time_t time_now = time(NULL); /* get the no. of second since epoch in time_now */  
	/* now get struct tm pointer from localtime based on seconds since epoch */
	struct tm * tm_now = localtime(&time_now);

	//sprintf(date_time, "%02d-%02d-%d %02d:%02d:%02d", tm_now->tm_hour,
	//	tm_now->tm_mon + 1, tm_now->tm_year + 1900, tm_now->tm_hour, 
	//	tm_now->tm_min, tm_now->tm_sec);
	strftime(date_time,18, "%D %T", tm_now);
	return date_time;
};

/*A wrapper function to send log file to client */
void sendlog(FILE * log, int sock_fd, int filesize) {
	char buffer[BUFFER_SIZE];
	int read_bytes, send_bytes;
	int remaining_bytes = filesize;
	while(remaining_bytes > 0) {
		read_bytes = fread(buffer, sizeof(char), sizeof(buffer), log);
		send_bytes = send(sock_fd, buffer, read_bytes, 0);
		if(send_bytes < 0) {
			perror("Sending Log");
			exit(EXIT_FAILURE);
		}

		remaining_bytes -= read_bytes;
	}
};

/* A wrapper to receive log file from client*/
void recvlog(FILE * log, int sock_fd, int filesize) {
	char buffer[BUFFER_SIZE];
	int wrote_bytes, recvd_bytes;
	int remaining_bytes = filesize;

	while(remaining_bytes > 0) {
		recvd_bytes = recv(sock_fd, buffer, sizeof(buffer), 0);
		wrote_bytes = fwrite(buffer, sizeof(char), recvd_bytes, log);

		remaining_bytes -= wrote_bytes;
	}

	if(recvd_bytes < 0) {
		perror("Receiving log");
		exit(EXIT_FAILURE);
	}
};

FILE * _initialise_log() {
	FILE * fp = fopen(LOGFILE_NAME, "r+");  /*try reading the log file */
	if(fp != NULL) {    /* if opened then return */
		return fp;
	}
    // else log file doesnot exist. Hence, create it.
	fp = fopen(LOGFILE_NAME, "w+");  /* mode is w+. we want to read and write both */ 
	
	/* Now we initialise the content of log file */
	fprintf(fp, "File(s) to be received:\n");
	fprintf(fp, "\n");
	fprintf(fp, "---------------------------------------------------------");
	fprintf(fp, "---------------------------------------------------------\n");
	fprintf(fp, "Filename \t\t\t\t Filesize \t Start Time \t\t Bytes Transferred\t"); 
	fprintf(fp, "%% Completed \t End Time \t\t No. of Connections \t No. of Timeouts");
	fprintf(fp, "\n---------------------------------------------------------");
	fprintf(fp, "---------------------------------------------------------\n");
	fflush(fp);
	return fp;
};

/* A wrapper function for recv() in order to wait for data until timer 
	expires */
int recv_with_timeout(int sock_fd, struct segment *buffer, size_t size, int timeout) {
	fd_set fds;  /* create the FD set for select() */
	int return_val;
	struct timeval to;  /* timer initialisation */

	FD_ZERO(&fds);   /* set the fd set to zero */
	FD_SET(sock_fd, &fds); /*add the sock_fd(the connected client socket) to fd set*/

	to.tv_sec = timeout;   /* set the timeout value */
	to.tv_usec = 0;

	return_val = select(sock_fd + 1, &fds, NULL, NULL, &to);  
	/* wait until timeout occurs or data is received */

	if(return_val == -1) return -1;
	if(return_val == 0) return TIMEOUT_OCCURED;

	return recv(sock_fd, buffer, size, 0);
};

/*utility function to get file size */
int _get_file_size(FILE * fp) {
	int size;
	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	return size;
};


/*this utlitity function helps to move seek to aspecified line number*/
void _goto_line_num_in_file(FILE * fp, int linenum) {
	fseek(fp, 0, SEEK_SET);  /* reset the files seek to beginning */
	int linecount = 1;
	long char_count = 0;
	char ch;
	/* loop through the file content until we reach the desired line or EOF*/
	while(linecount != linenum && ((ch = getc(fp)) != EOF)) {
		char_count++;  /*keeps record of bytes being read so that we can seek 
		to that position */
		if(ch == '\n') {  /* if we get a newline character we've got a new line */
			linecount++;
		}

	}
	/*we come out of loop when we reach the sepcified line number or EOF */
	fseek(fp, char_count, SEEK_SET); /* finally seek to the desired byte number 
	which is the starting byte of our desired line number*/
};


/* this functions helps to replace the content  of a specifed line number
 which a provided string or stream of characters */
void _replace_line(FILE *fp, int linenum, char * new_content) {
	fseek(fp, 0, SEEK_SET); /*seek to beginning of file*/
	FILE * temp = fopen("tmp", "w"); /* create a temporary file*/
	int linecount = 1;
	char ch;
	while((ch = getc(fp)) != EOF) {  /* we copy the entire content of original
	file to this temp file, except for the line where replacement has to be done */
		putc(ch, temp);
		if(ch == '\n') {
				linecount++;
		}
		if(linecount == linenum) { /* when we reach the desired line */
			fprintf(temp, "%s", new_content); /* we put provided new content at 
			this line */
			while(((ch = getc(fp)) != '\n') && (ch != EOF)); /* and we skip all the 
			bytes of this line in the original file*/
			fseek(fp, -1, SEEK_CUR); /* we seek back one position so that we 
			can also put the newline character in temp file and update newline variable
			so that it can't match our desired line number in next iterations*/
		}
	}
	fclose(temp);
	fclose(fp);
	remove(LOGFILE_NAME);   /* delete the original file */
	rename("tmp", LOGFILE_NAME); /* rename the temp file to name of original file. now
	 this is our updated original file*/
	fp = fopen(LOGFILE_NAME, "r+"); /*we reopen this updated original file 
	so that we can further manipulate it */
}

char * _get_line_as_string(FILE * file, int linenum) {

	_goto_line_num_in_file(file, linenum);  /* bring sek to the specified linenum*/
	
	char * linestring;
	char ch;
	int line_length = 0; /* for storing number of character in line 2*/
	while((ch = getc(file)) != '\n') { /* read line till the end of line */
		line_length++;
	} 
	/* now we have no. of character in the line. So we can allocate a memory
	sufficient to hold this line and then store the line in a string */

	linestring = (char *)malloc(line_length);
	_goto_line_num_in_file(file, linenum);  /* we again position the seek to the
	beginning of the specified */
	fgets(linestring, line_length, file); /*we store line content into linestring*/

	return linestring;
}

/* This function syncs the files-to-be-uploded data in server log with client log*/
void _sync_uncommon_files_with_client_log(FILE * server_log, FILE * client_log) {

	/* seek to line 2 of client log, because at this line files-to-be-uploaded list 
	is recorded. then read the content of this line which will serve as new content
	for the replacement in server log file */
	char * new_content = _get_line_as_string(client_log, 2);
	/* we do the replacement of line 2 in server with the content 
	extracted from line 2 of client log*/
	_replace_line(server_log, 2, new_content);
};

void _update_transfer_progress_in_log(server_log * log_entry, char * f_name,
	unsigned int bytes_transferred, short percentage, FILE * log_file,
	short flag) {
	
	server_log temp;

	/*first we move to part from where structure entry has to start */
	_goto_line_num_in_file(log_file, FILE_RECORD_LINE_NUMBER);
	
	while(fread(&temp, sizeof(server_log), 1, log_file)) {
		if(strcmp(temp.filename, f_name) == 0) {
				/* first collect and store all the 
			entries already in the logfile */ 
			strcpy(log_entry->filename, temp.filename);
			strcpy(log_entry->filesize, temp.filesize);
			strcpy(log_entry->start_time, temp.start_time);
			log_entry->connection_count = temp.connection_count;
			log_entry->bytes_transferred = temp.bytes_transferred;
			log_entry->percentage_completion = temp.percentage_completion;
			strcpy(log_entry->end_time, temp.end_time);
			log_entry->timeout_count = temp.timeout_count;	

			/* then we update particular entries bvased on flag*/
			if(flag == UPDATE_LOG_TIMEOUT) {
				log_entry->timeout_count = temp.timeout_count + 1;	
			}
			else if(flag == UPDATE_LOG_COMPLETED) {
				log_entry->bytes_transferred = bytes_transferred;
				log_entry->percentage_completion = percentage;
				strcpy(log_entry->end_time, _get_current_date_time());	
			}
			else if (flag == UPDATE_LOG_PROGRESS){
				log_entry->bytes_transferred = bytes_transferred;
				log_entry->percentage_completion = percentage;
			}
			else if(flag == UPDATE_LOG_CONNECTION_COUNT) {
				log_entry->connection_count = temp.connection_count + 1;
			}
			
			/* seek to the beginning of record */
			fseek(log_file, -1 * sizeof(server_log), SEEK_CUR);
			/*update the record*/
			fwrite(log_entry, sizeof(server_log), 1, log_file);

			/* escape from loop once updation is done */
			break;
		}
	}
};

/* the function initialises all fields of server log structure with initial
information about file being received*/
short _initialise_log_entry_for_file(server_log * log_entry, char * f_name, 
	char * f_size, FILE * log_file, long int * bytes_uploaded) {

	/*first we move to part from where structure entry has to start */
	_goto_line_num_in_file(log_file, FILE_RECORD_LINE_NUMBER);

	/*initialise log_entry only when it is not present in log */
	while(fread(log_entry, sizeof(server_log), 1, log_file)) {
		/* if filename already present in file structure 
		then do not initialise */
		if(strcmp(log_entry->filename, f_name) == 0) {
			/* if file_name is already in the entry that means, 
			client is re-attempting to upload a broken file. Hence
			update the connection count and return without
			initialising.*/
			*bytes_uploaded = log_entry->bytes_transferred;
			_update_transfer_progress_in_log(log_entry, f_name, 
				0, 0, log_file, UPDATE_LOG_CONNECTION_COUNT);
			return REATTEMPT_UPLOAD;
		}
	}
	strcpy(log_entry->filename, f_name);
	strcpy(log_entry->filesize, f_size);
	strcpy(log_entry->start_time, _get_current_date_time());
	strcpy(log_entry->end_time, "\0");
	log_entry->bytes_transferred = 0;
	log_entry->percentage_completion = 0;
	log_entry->connection_count = 1;
	log_entry->timeout_count = 0;

	/* seek log file to the last */
	fseek(log_file, 0, SEEK_END);
	/* make the entry */
	fwrite(log_entry, sizeof(server_log), 1, log_file);
};

/* an utility function to remove a substring from string. (Just one ocurrance) 
used when we have to remove the successfully uploaded file from the list
of files to be uploaded.*/
char * _remove_from_string(char * string, char *substring) {
	int len = strlen(substring);
	char * temp; 

	if(len > 0) {
		temp = strstr(string, substring);

		if(temp != NULL) {
			memmove(temp, temp + len, strlen(len + temp) + 1);
		}
	}
	return string;
};

void _update_file_to_be_received_list(FILE * server_log, char * filename) {
	/*we will first read line 2 as string and remove the 
		current filename from this string. then we will replace line 2 of the 
		server log file with this new string.*/
	char * line = _get_line_as_string(server_log, 2);
	char * updated_line = _remove_from_string(line, filename);

	_replace_line(server_log, 2, updated_line);	
};

void printlog(FILE * log_file) {
	char ch;
	int linenum = 1;
	fseek(log_file, 0 ,SEEK_SET);

	while((ch = getc(log_file)) != EOF) {
		if(ch == '\n') {
			linenum++;
		}
		printf("%c", ch);
		if(linenum == FILE_RECORD_LINE_NUMBER) {
			break;
		}
	}
	server_log log_entry;
	char * endtime;
	while(fread((&log_entry), sizeof(server_log), 1, log_file)) {
		if(strcmp(log_entry.end_time, "\0") == 0) {
			endtime = "NA";
		}
		else {
			endtime = log_entry.end_time;
		}
		printf("%-35s\t%-15s\t%-20s\t%-20lu\t%i%%\t%20s\t%20lu\t%20lu\n", 
			log_entry.filename,
			log_entry.filesize,
			log_entry.start_time,
			log_entry.bytes_transferred,
			log_entry.percentage_completion,
			endtime,
			log_entry.connection_count,
			log_entry.timeout_count);
	}
};

int main(int argc, char * argv[]) {
	int server_sock, connected_client_sock;
	struct sockaddr_in server_addr, client_addr;
	int size_server_addr = sizeof(server_addr);

	//for received file stats
	int filesize;
	char buffer[BUFFER_SIZE] = {0};
	char filename[FILENAME_SIZE];

	//declaring the structure for segment and recvd segment
	struct segment server_segment = {0};
	struct segment recvd_segment = {0};

	//File pointer to open a file to write the data received from stream
	FILE * recvd_file, * log_file;

	//Retransmission timeout value
	short RTO;   /* initial value will be 3 sec which will be doubled each retry */
	short retry;   /* sender will retry sending acc to this value */

	//resetting the both address structure to zero
	memset(&server_addr, 0, size_server_addr);
	memset(&client_addr, 0, sizeof(client_addr));

	int recvd_bytes, remaining_file, wrote_bytes;
	int logfile_size, temp_log_file_size;

	log_file = _initialise_log(); /* create log file if Doesn't exist and return*/

	/* if command line has some argument process that */
	if(argc > 1) {
		if(strcmp("--log",argv[1]) == 0) { 
		/*if --log flag is used show logs on STDOUT.*/
			printlog(log_file);
			exit(EXIT_SUCCESS);
		}
	}


	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(server_sock < 0) {
		perror("Socket error");
		exit(EXIT_FAILURE);
	}

	fflush(stdout);  /* We are flushing it so that we can immediately print 
	on any file, as socket will be buffering anything written to files,
	 and not print until it gets a connection.*/

	server_addr.sin_family = PF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	int yes = 1;
	if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		perror("Bind Settings");
		exit(EXIT_FAILURE);
	}

	int bind_res = bind(server_sock, (struct sockaddr_in *) &server_addr,
		 size_server_addr);
	if(bind_res < 0) {
		perror("Bind");
		exit(EXIT_FAILURE);
	}

	if(listen(server_sock, BACKLOG) < 0) {
		perror("Listening");
		exit(EXIT_FAILURE);
	}

	printf("Server Binded on port %d \n", PORT);
	printf("\nServer is listening for connection ...\n");

	fflush(stdout);
	connected_client_sock = accept(server_sock, (struct sockaddr_in *) &client_addr,
		(socklen_t *) &size_server_addr);
	if(connected_client_sock < 0) {
		perror("Connection");
		exit(EXIT_FAILURE);
	}

	//getting the ip address and port of client
	char client_ip[INET_ADDRSTRLEN];
	if(inet_ntop(AF_INET, &client_addr.sin_addr,
	 client_ip, INET_ADDRSTRLEN) == NULL) {
	 	perror("Address transalation");
	 	exit(EXIT_FAILURE);
	}

	int client_port = ntohs(client_addr.sin_port);

	printf("\nServer is connected to: %s at port %u\n", client_ip, client_port);

	/* Once connected we first exchange the log files. Received log file 
	from client is saved as temp.txt */
	FILE * temp_log = fopen(RECEIVED_LOG, "w+"); /* w+ mode as we need to read too*/
	if(temp_log == NULL) {
		printf("File error");
		exit(EXIT_FAILURE);
	}

	/* We will first send the size of log file and also receive the logfile size
	from other side. This will be our protocol to stop receiving once the whole 
	filesize has been received. 
	MAJOR BUG HERE: recv in recvlog function was waiting
	for a data which was already received. And there is no way to tell recv that when
	to stop waiting for receive without the filesize. */

	/*sending logfile size*/
	logfile_size = _get_file_size(log_file);
	sprintf(buffer, "%d", logfile_size);
	send(connected_client_sock, buffer, sizeof(buffer), 0);
	
	/*Recieving log file size of client*/
	recv(connected_client_sock, buffer, sizeof(buffer), 0);
	temp_log_file_size = atoi(buffer);

	/*Now sending the log file and also receiving from the client*/
	sendlog(log_file, connected_client_sock, logfile_size);
	recvlog(temp_log, connected_client_sock, temp_log_file_size);

	/*Server now syncs the info about file to be uploaded by client.
	Server gets the information of files waiting to be uploaded in client
	directory via this function only, by reading and synching with the
	client log, which was just received.*/
	_sync_uncommon_files_with_client_log(log_file, temp_log);

	//start receiving from client
	//first, receive filename and filesize
	recvd_bytes = recv(connected_client_sock, (struct segment *)&recvd_segment,
		sizeof(struct segment), 0);

	if(recvd_bytes > 0) {
		printf("File to be received: %s \n",recvd_segment.filename);
		printf("Size: %s B \n",recvd_segment.filesize);
	}	

	// if we haven't received anything yet, the connection might be closed
	if(recvd_bytes == 0) {
		printf("\nConnection closed by client.\n");
		exit(EXIT_SUCCESS);
	}
	else if(recvd_bytes < 0) {  /* else if value is negative, there's some error */
		perror("Receiving file metadata");
		exit(EXIT_FAILURE);
	}

	//now start writing the file
	recvd_file = fopen(recvd_segment.filename, "a");
	if(recvd_file == NULL) {
		perror("File Creation");
		exit(EXIT_FAILURE);
	}

	/* We have received filesize as char buffer from socket.
		So we convert it to int and store. */
	filesize = atoi(recvd_segment.filesize);

	strcpy(filename, recvd_segment.filename); /* just storing for convenience */

	/*variable to decide upto when we have to receive and progress */
	if(filesize == 0) {
		printf("\nCouldn't receive data properly. Exiting.\n");
		exit(EXIT_FAILURE);
	}
	
	/* these are few variables being used in loop for collecting
	qunatitative data about transfer and also control the loop*/
	remaining_file = filesize;
	unsigned long bytes_transferred = 0;
	unsigned short percentage = 0;

	server_log initial_log_entry, log_entry;   /* creating instances of 
	server log for initialising and updating*/
	/* we now initialise the server_log_entry for the provided file transfer */

	long int amount_uploaded = -1; /* it contains the bytes transferred from the 
	log file in case, this is an re-attempt to upload */
	short init_result = _initialise_log_entry_for_file(&initial_log_entry, 
		filename, recvd_segment.filesize, log_file, &amount_uploaded);

	if(init_result == REATTEMPT_UPLOAD) { /* If it is an reattempt then we need to
	make some arrangements*/
		if(amount_uploaded == -1) {
			printf("\nSome error ocurred. Exiting.\n");
			exit(EXIT_FAILURE);
		}

		bytes_transferred = amount_uploaded;
		remaining_file = filesize - bytes_transferred;

		server_segment.ack_no = (amount_uploaded/BUFFER_SIZE);
	}


	while(remaining_file > 0 && recvd_bytes > 0) {
		retry = 3;
		RTO = 3;
		while(retry > 0) {   /*we will try resending 3 times in case of failure */
			recvd_bytes = recv_with_timeout(connected_client_sock, 
				(struct segment *)&recvd_segment, 
				sizeof(struct segment), RTO);

			if(recvd_bytes == 0) {
				printf("\nConnection closed by client.\n");
				break;
			}
			else if(recvd_bytes == -1) {
				perror("Timeout");
				exit(EXIT_FAILURE);
			}
			else if(recvd_bytes == TIMEOUT_OCCURED) {
				printf("\nTimeout ocurred. Retrying ...\n");
				retry--;
				RTO *= 2; 

				/*as the timeout has ocurred we will update the corresponding
				record in log file*/
				_update_transfer_progress_in_log(&log_entry, filename, 
						0, 0, log_file, UPDATE_LOG_TIMEOUT);
				/* we provide the timeout argument of the function as 1 
				so that function gets to know only timeout has to be updated */
			}
			else {  /*else we have got some data */
				// if the segment received was being expected by the server

				if(recvd_segment.seq_no == server_segment.ack_no) {
					//seeking the file at right position
					fseek(recvd_file, ((recvd_segment.seq_no)*BUFFER_SIZE), SEEK_SET);

					//write to file and increment acknowledgement no.
					wrote_bytes = fwrite(recvd_segment.buffer, sizeof(char),
						sizeof(recvd_segment.buffer), recvd_file);

					/* now we want next sequence */
					server_segment.ack_no = recvd_segment.seq_no + 1;
					/* current seq is received */
					server_segment.seq_no = recvd_segment.seq_no; 
					printf("\nReceived Sequence No: %d", recvd_segment.seq_no);

					//we now calculate how much of file is left to be received
					remaining_file = remaining_file - wrote_bytes;

					/* as new segment has been written to file, we will update
					the records in corresponding log file*/
					//TODO
					bytes_transferred += wrote_bytes;
					percentage = (bytes_transferred/(float)filesize)*100;
					_update_transfer_progress_in_log(&log_entry, filename, 
						bytes_transferred, percentage, log_file, 
						UPDATE_LOG_PROGRESS);
				}

				//now send the acknowledgement with proper ack_no
				if(remaining_file > 0) {
					send(connected_client_sock, (void *)&server_segment, 
						sizeof(struct segment), 0);
					printf("\nSending Acknowledgement no: %d\n",
						 server_segment.ack_no);	
				}
				
				break; /* now no need to retry for this segment */
			}
		}

		if(retry == 0) {
			printf("\nConnection Lost\n");
			exit(EXIT_FAILURE);
		}

		
		printf("\nReceived %d Bytes\n", bytes_transferred);
	}
	if(remaining_file <= 0){
		printf("\nFile received successfully.\n");

		/* update the log record on completion of file transfer */
		_update_transfer_progress_in_log(&log_entry, filename, 
						filesize, 100, log_file, UPDATE_LOG_COMPLETED);

		/* now, we also need to remove the file entry from line 2 as the
		list should contain the files which are not completely received*/
		_update_file_to_be_received_list(log_file, filename);
	}

	fclose(recvd_file);
	close(server_sock);
	close(connected_client_sock);

	return 0;
}