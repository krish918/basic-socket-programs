#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#define PORT 6060
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1400
#define FILENAME_SIZE 72
#define TIMEOUT_OCCURED -2  /* a constant to signal timeout has ocurred */
#define FILESIZE_STRING 11 /* We want atmost 9,999,999,999 Bytes of file 9.9GB*/
#define LOGFILE_NAME "client_log"
#define RECEIVED_LOG "temp"
#define FILE_RECORD_LINE_NUMBER 6 /*as we are writing both, plain text and 
structure to the same log file. Hence are storing the line number from where
structure record entry is starting.*/
#define FULLY_UPLOADED 100
#define PARTIALLY_UPLOADED 50
#define NEW_UPLOAD 0

/* These constants will be used as flag to decide what data in the logfile
has to be udated corresponding to a file*/
#define UPDATE_LOG_PROGRESS 0 
#define UPDATE_LOG_COMPLETED 1
#define UPDATE_LOG_TIMEOUT 2
#define UPDATE_LOG_CONNECTION_COUNT 3

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
} client_log;

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

/*A wrapper function to send log file to server */
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

/* A wrapper to receive log file from server */
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
		if(ch == '\n')  /* if we get a newline character we've got a new line */
			linecount++;
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
			fprintf(temp, "%s", new_content); /* we put provided new content
			 at this line */
			while(((ch = getc(fp)) != '\n') && (ch != EOF)); /* and we skip all the 
			bytes of this line in the original file*/
			fseek(fp, -1, SEEK_CUR); /* we seek back one position so that we 
			can also put the newline character in temp file and update 
			newline variable
			so that it can't match our desired line number in next iterations*/
		}
	}
	fclose(temp);
	fclose(fp);
	remove(LOGFILE_NAME);   /* delete the original file */
	rename("tmp", LOGFILE_NAME); /* rename the temp file to the name of 
	original file. now this is our updated original file*/
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

int _check_uploaded(char * filename, FILE * log) {
	int uploaded = 0;
	//TODO
	client_log log_entry;
	/* let's move to line from which structure entries start */
	_goto_line_num_in_file(log, FILE_RECORD_LINE_NUMBER);

	/* then we iterate through each record and search 
	for entry with the provided file name */
	while(fread(&log_entry, sizeof(client_log), 1, log)) {
		if(strcmp(log_entry.filename, filename) == 0 && 
			log_entry.percentage_completion == 100) {
			/* if file found and percentage completion is 100
			 that means file is completely uploaded */
			uploaded = 1;
			break;
		}
	}

	return uploaded;
}

/* This utility function scans the current client directory 
to get all the files available for upload as a formatted string*/
char * _get_files_to_be_uploaded(FILE * log) {
	char *list = "\0", * buf; /*list will contain final string*/
	char * temp; /* holds the formatted filename string */
	struct dirent *dir;
	DIR *dd = opendir(".");  /*Open the current directory*/
	if(dd) {
		while((dir = readdir(dd)) != NULL) { /*get all files in current dir*/
 			/*we filter out some of the filenames.*/
			if((strcmp(dir->d_name, "fclient") != 0) && 
					(strcmp(dir->d_name, ".") != 0)  && 
					(strcmp(dir->d_name, "..") != 0) &&
					(strcmp(dir->d_name, LOGFILE_NAME) != 0) &&
					(_check_uploaded(dir->d_name, log)) == 0) {  /* if the file is 
					uploaded succesfully it must not appear in the list*/
				
				/*We hold the formatted string of filename in temp*/
				temp = (char *)malloc(strlen(dir->d_name) + 4);
				sprintf(temp, "'%s'\t", dir->d_name);

				/*We allocate enough memory to the buffer */
				buf = (char *)malloc(strlen(temp) + strlen(list) + 1);
				/* we put the list into buffer. List contains the string creted 
				till now */
				strcpy(buf, list);
				/* Then we concat the temporray formatted string which contains
				the current filename, to the buf. Hence buf contain the latest
				full filename list */
				strcat(buf, temp);

				/* We put back the buf into the list */
				list = buf;
				
			}
		}
		/* return the prepared string of files to be uploaded */
		return list;
	}
};

FILE * _initialise_log() {
	FILE * fp = fopen(LOGFILE_NAME, "r+");  /*try reading the log file */
	if(fp != NULL) {    /* if opened then return file pointer
	 but, just update the files to be uploaded as there could be new files
	  in the directory */
		/* List of files to be sent is at line 2 of the file. 
		  We replace this line with a new list of files. */
		_replace_line(fp, 2, _get_files_to_be_uploaded(fp)); 

		fseek(fp, 0L, SEEK_SET); /* Reset the seek to start */
		return fp;
	}
    // else log file doesnot exist. Hence, create it.
	fp = fopen(LOGFILE_NAME, "w+");  /* mode is w+. we want to read and write both */ 
		
	/* Now we initialise the content of log file */
	fprintf(fp, "File(s) to be sent:\n");
	/* we get and print all the files to be uploaded*/ 
	fprintf(fp, _get_files_to_be_uploaded(fp));
	fprintf(fp, "\n---------------------------------------------------------");
	fprintf(fp, "---------------------------------------------------------\n");
	fprintf(fp, "Filename \t\t\t\t Filesize \t Start Time \t\t"); 
	fprintf(fp, " Bytes Transferred \t %% Completed \t End Time \t\t"); 
	fprintf(fp, " No. of Connections \t No. of Timeouts");
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

void _update_transfer_progress_in_log(client_log * log_entry, char * f_name,
	unsigned int bytes_transferred, short percentage, FILE * log_file,
	short flag) {
	
	client_log temp;

	/*first we move to part from where structure entry has to start */
	_goto_line_num_in_file(log_file, FILE_RECORD_LINE_NUMBER);
	
	while(fread(&temp, sizeof(client_log), 1, log_file)) {
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
			fseek(log_file, -1 * sizeof(client_log), SEEK_CUR);
			/*update the record*/
			fwrite(log_entry, sizeof(client_log), 1, log_file);

			/* escape from loop once updation is done */
			break;
		}
	}
};

/* the function initialises all fields of server log structure with initial
information about file being received*/
/*THIS DEFINITION IS SLIGHTLY DIFFERENT FROM SERVER COUNTER PART 
as we also have to check whether the upload was interrupted earlier */
short _initialise_log_entry_for_file(client_log * log_entry, char * f_name, 
	char * f_size, FILE * log_file, 
	FILE* serv_log,
	long int * bytes_uploaded) {

	/*first we move to part from where structure entry has to start */
	_goto_line_num_in_file(log_file, FILE_RECORD_LINE_NUMBER);

	/*initialise log_entry only when it is not present in log */
	while(fread(log_entry, sizeof(client_log), 1, log_file)) {
		/* if filename already present in file structure 
		then do not initialise */
		if(strcmp(log_entry->filename, f_name) == 0) {

			/* if filename is entered in log, that means already an attempt
			to upload has taken place. 
			*/
			/* if file is completly uploaded return the signal 
			of fully uploaded, else read the server log for how much 
			bytes have been succesfully transferred. */
			if(log_entry->percentage_completion == 100) {
				return FULLY_UPLOADED;
			}

			client_log temp_log_entry;

			_goto_line_num_in_file(serv_log, FILE_RECORD_LINE_NUMBER);
			while(fread(&temp_log_entry, sizeof(client_log), 1, serv_log)) {
				if(strcmp(temp_log_entry.filename, f_name) == 0) {
					/* found the entry in server log*/
					/* now store the no. of bytes uploaded */
					*bytes_uploaded = temp_log_entry.bytes_transferred;
					return PARTIALLY_UPLOADED;
				}
			}
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
	fwrite(log_entry, sizeof(client_log), 1, log_file);

	return NEW_UPLOAD;
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

void _update_file_to_be_received_list(FILE * client_log, char * filename) {
	/*we will first read line 2 as string and remove the 
		current filename from this string. then we will replace line 2 of the 
		server log file with this new string.*/
	char * line = _get_line_as_string(client_log, 2);
	char * updated_line = _remove_from_string(line, filename);

	_replace_line(client_log, 2, updated_line);	
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
	client_log log_entry;
	char * endtime;
	while(fread((&log_entry), sizeof(client_log), 1, log_file)) {
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
	int client_sock;
	struct sockaddr_in server_addr;

	FILE * file_to_send, *log_file;

	/* We are initialsing two segments one corresponding to what we send
	and other one we receive. */
	struct segment client_segment = {0};
	struct segment recvd_segment = {0};

	char filename[BUFFER_SIZE];
	long int filesize;
	char filesize_to_send[BUFFER_SIZE];
	char buffer[BUFFER_SIZE] = {0};
	int remaining_bytes, read_bytes;
	int sent_bytes, recvd_bytes;  
	int logfile_size, temp_log_file_size;

	//Retransmission timeout value
	short RTO;   /* initial value will be 3 sec which will be doubled each retry */
	short retry;   /* sender will retry sending acc to this value */

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	
	//converting the ip address in plain text to network format and
	//  storing it in server_addr.sin_addr
	if(inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) < 0) {
		perror("Address Conversion");
		exit(EXIT_FAILURE);
	}

	log_file = _initialise_log(); /* create log file if it doesn't exist 
	and scan the directory to collect all files which are to be uploaded, 
	and add that to the log. If created then simply return the 
	file for reading/writing.*/

	/* if command line has some argument process that */
	if(argc < 2) {
		printf("\nNo filename or flag provided.\n");
		printf("\nUSAGE: ./fclient [filename | Flag]\n\n");
		exit(EXIT_SUCCESS);
	}

	if(strcmp("--log",argv[1]) == 0) { 
		/*if --log flag is used show logs on STDOUT.*/
		printlog(log_file);
		exit(EXIT_SUCCESS);
	}


	//now creating a socket for this process so that 
	//   it can connect to the server through this socket.
	client_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(client_sock <  0) {
		perror("Socket");
		exit(EXIT_FAILURE);
	}

	int conn_res = connect(client_sock, (struct sockaddr_in *) &server_addr,
		sizeof(server_addr));
	
	if(conn_res < 0) {
		perror("Connection");
		exit(EXIT_FAILURE);
	}

	printf("Connected to server %s at port %d.\n", SERVER_IP, PORT);

		/* Once connected we exchange the log files. Recieved log file from server 
	is saved as temp.txt*/
	FILE * temp_log = fopen(RECEIVED_LOG, "w+");
	if(temp_log == NULL) {
		printf("File error");
		exit(EXIT_FAILURE);
	}

	/*We will first receive the log file size from server and then 
	send the size of client log file. This will help us to decide when 
	to stop receiving from the tcp stream while exchanging the log file*/

	/* First we receive the file size of server log as we will be
	first receiving the log from server */
	recv(client_sock, buffer, sizeof(buffer), 0);
	temp_log_file_size = atoi(buffer);

	/*getting the filesize of client log and sending it*/
	logfile_size = _get_file_size(log_file);
	sprintf(buffer, "%d", logfile_size);
	send(client_sock, buffer, sizeof(buffer), 0);

	/*Now we start receiving the log file from server*/
	recvlog(temp_log, client_sock, temp_log_file_size);
	sendlog(log_file, client_sock, logfile_size);

	
	//opening the file to be sent
	strcpy(client_segment.filename, argv[1]);
	strcpy(filename, argv[1]); /* storing for convenience */
	file_to_send = fopen(client_segment.filename, "r");
	if(file_to_send == NULL) {
		perror("File");
		exit(EXIT_FAILURE);
	}

	//getting size of file by seeking to the end of file
	filesize = _get_file_size(file_to_send);

	//storing and printing filesize
	sprintf(client_segment.filesize, "%d", filesize);
	printf("\nFile Size: %s Bytes \n", client_segment.filesize);

	//sending file name & size to server
	sent_bytes = send(client_sock, (void *)&client_segment, 
		sizeof(struct segment), 0); 
		
	if(sent_bytes < 0) {
		perror("Sending file metadata");
		exit(EXIT_FAILURE);
	}

	/* these are few variables being used in loop for collecting
	qunatitative data about transfer and also control the loop*/
	remaining_bytes = filesize;
	recvd_bytes = 1;   /* setting just to start the loop */
	unsigned long bytes_transferred = 0;
	unsigned short percentage = 0;

	client_log initial_log_entry, log_entry;   /* creating instances of 
	server log for initialising and updating*/

	/* we now initialise the server_log_entry for the provided file 
	and also check whether the file is already uploaded or partially
	uploaded */
	long int amount_uploaded = -1;
	short init_result = _initialise_log_entry_for_file(&initial_log_entry, 
		filename, client_segment.filesize, log_file, temp_log, 
		&amount_uploaded);

	if(init_result == FULLY_UPLOADED) {
		printf("\nFile is already uploaded. Check logs for more detail.\n");
		exit(EXIT_SUCCESS);
	}
	else if(init_result == PARTIALLY_UPLOADED) {
		if(amount_uploaded == -1) {
			printf("\nSome error ocurred. Exiting.\n");
			exit(EXIT_FAILURE);
		}

		/* NOW WE TRY TO RESUME THE UPLOAD PROCESS */

		/*now we have the amount uploaded. We will use to 
		set the sequence number and according to that, file will be
		seeked at correct position.*/

		client_segment.seq_no = (amount_uploaded/BUFFER_SIZE);
		client_segment.ack_no = client_segment.seq_no;
		/* We will resume reading file at psoition indicated by
		the updated seq_no. We will also update the connection count
		in client_log*/

		/* update the various loop variables to reflect correct value 
		after resuming*/
		bytes_transferred = amount_uploaded;
		remaining_bytes = filesize - amount_uploaded;
		percentage = (bytes_transferred/(float)filesize)*100;

		/* we update the connection count in log file and also 
		update the progress of file according to server record in
		client log*/

		_update_transfer_progress_in_log(&log_entry, filename, 
						0, 0, log_file, UPDATE_LOG_CONNECTION_COUNT);

		_update_transfer_progress_in_log(&log_entry, filename, 
						bytes_transferred, percentage, log_file, 
						UPDATE_LOG_PROGRESS);	
	}
	
	while(remaining_bytes > 0 && recvd_bytes > 0) {
		retry = 3;
		RTO = 3;
		//Setting the file pointer at right position acc to seq no.
		fseek(file_to_send, ((client_segment.seq_no)*BUFFER_SIZE), SEEK_SET);

		//read buffersize amount of bytes from file into the segment buffer
		read_bytes = fread(client_segment.buffer, sizeof(char), 
			sizeof(client_segment.buffer), file_to_send);
		if(read_bytes < 0) {
			perror("File read");
			exit(EXIT_FAILURE);
		}
		while(retry > 0) {
			// send the segment to server
			sent_bytes = send(client_sock, (void *)&client_segment, 
				sizeof(struct segment), 0);
			if(sent_bytes < 0) {
				perror("Sending File");
				exit(EXIT_FAILURE);
			}

			printf("\nSent Sequence no: %d", client_segment.seq_no);

			// now we wait for acknowledge from the receiver
			recvd_bytes = recv_with_timeout(client_sock, 
				(struct segment *)&recvd_segment, 
				sizeof(struct segment), RTO);
			/* using sizeof r_segment as recvd_segment is a pointer */

			if(recvd_bytes == 0) {
				printf("\nConnection closed.\n");
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
			else { /* Now we have got the acknowledgement from server*/
				if(client_segment.seq_no == recvd_segment.seq_no) {

					client_segment.seq_no = recvd_segment.ack_no;
					client_segment.ack_no = recvd_segment.ack_no;
				}
				printf("\nReceived Acknowledgement for sequence no: %d", 
					recvd_segment.seq_no);
				printf("\nReceived Acknowledgement no: %d\n", recvd_segment.ack_no);

				/* as new segment has been sent, we will update
					the records in corresponding log file*/

				bytes_transferred += read_bytes;
				percentage = (bytes_transferred/(float)filesize)*100;
				_update_transfer_progress_in_log(&log_entry, filename, 
						bytes_transferred, percentage, log_file, 
						UPDATE_LOG_PROGRESS);

				break; /* now no need to retry for this segment */
			}
		}

		remaining_bytes = remaining_bytes - read_bytes;
		printf("\nRemaining: %d Bytes", remaining_bytes);
	}
	if(remaining_bytes <= 0) {
		printf("\nFile sending Completed.\n");

		/* update the log record on completion of file transfer */
		_update_transfer_progress_in_log(&log_entry, filename, 
						filesize, 100, log_file, UPDATE_LOG_COMPLETED);

		/* now, we also need to remove the file entry from line 2 as the
		list should contain the files which are not completely received*/
		_update_file_to_be_received_list(log_file, filename);
	}
	
	fclose(file_to_send);
	close(client_sock);

	return 0;
}