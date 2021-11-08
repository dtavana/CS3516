/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10	 // how many pending connections queue will hold

// DEFAULTS
// Default port
#define PORT 2012
// Default number of requests for rate limiting
#define NUM_REQUESTS 3
// Default number of requests per seconds (NUM_REQUESTS / NUM_SECONDS = rate limit)
#define NUM_SECONDS 60
// Default number of users to connect at a time
#define NUM_USERS 3
// Default timeout in seconds for a client
#define TIMEOUT 80

// SETTINGS
// Number of characters in filename identifier
#define NUM_IDENTIFIER 6
// Max line length when decoding QR Codes
#define MAX_LINE_LENGTH 100
// Max URL Length
#define MAX_URL_LENGTH 100
// Out directory for created files
#define OUT_DIR "~/projects/Project1/out"


void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Modified from https://www.codeproject.com/Answers/640199/Random-string-in-language-C#answer1
void genrandom(char *s, int len) {
    char* alphanum = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    s[len] = 0;
}

void receive(int socket, uint32_t size, void* saveStruct) {
    uint32_t bytesRecv, totalBytesRecv = 0;
	int cnt = 0;
    while(totalBytesRecv < size) {
		bytesRecv = recv(socket, saveStruct, size, 0);
        if (bytesRecv == -1) {
			perror("receive");
			exit(1);
        }
		cnt++;
		totalBytesRecv += bytesRecv;
		saveStruct += bytesRecv;
		printf("Received %d/%d bytes on the iteration #%d | Total received: %d\n", bytesRecv, size, cnt, totalBytesRecv);
    }
}

void save_to_temp_file(char* data, int size, char* filename) {
	FILE* fp;
	char randomstring[NUM_IDENTIFIER];
	genrandom(randomstring, NUM_IDENTIFIER);
	sprintf(filename, "out/%s.png", randomstring);

	fp = fopen(filename, "wb");
	int bytesWritten = fwrite(data, 1, size, fp);
	fclose(fp);
	printf("Succesfully wrote %d bytes to %s\n", bytesWritten, filename);
}

int decodeImage(char* filename, char* res) {
	char command[400] = {0};
	sprintf(command, "java -cp javase.jar:core.jar com.google.zxing.client.j2se.CommandLineRunner %s", filename);
	printf("Command: %s\n", command);

	FILE* cmdout = popen(command, "r");
	if(!cmdout) {
		printf("popen failed");
		exit(1);
	}
	char line[MAX_LINE_LENGTH];
	while(!feof(cmdout)) {
		if(fgets(line, MAX_LINE_LENGTH, cmdout) != NULL) {
			if(strcmp(line, "Parsed result:\n") == 0) {
				fgets(line, MAX_LINE_LENGTH, cmdout);
				line[strlen(line) - 1] = '\0';
				strcpy(res, line);
				pclose(cmdout);
				return 1;
			}
		}
	}
	pclose(cmdout);
	return 0;
}

void runInteraction(int sockfd) {
	// Receive file size
		uint32_t filesize;
		receive(sockfd, sizeof(uint32_t), &filesize);
		printf("server: received filesize of '%d'\n", filesize);
		char* data = (char *) malloc(4096);
		receive(sockfd, filesize, data);
		printf("server: received filedata of '%s'\n", data);
		// NUM_IDENTIFIER + 4 for 'out/' + 4 for '.png'
		char* filename = (char *) malloc(NUM_IDENTIFIER + 4 + 4);
		save_to_temp_file(data, filesize, filename);
		char url[MAX_URL_LENGTH];
		int imageprocessres = decodeImage(filename, url);
		uint32_t servercode;
		uint32_t urllength;
		if(imageprocessres == 1) {
			// Succesful decode
			printf("Succesfully decoded to URL: %s\n", url);
			servercode = 0;
			urllength = MAX_URL_LENGTH;
		} else {
			// Unsuccesful decode
			printf("Could not decode QR Code\n");
			servercode = 1;
			urllength = 0;
		}
		// Send server code
		if(send(sockfd, &servercode, sizeof(uint32_t), 0) == -1) {
			perror("send servercode");
		}
		// Send urllength
		if(send(sockfd, &urllength, sizeof(uint32_t), 0) == -1) {
			perror("send urllength");
		}
		if(urllength > 0) {
			if(send(sockfd, &url, urllength, 0) == -1) {
				perror("send url");
			}
		}
}

void printPIDArr(pid_t* arr, int size) {
	for(int i = 0; i < size; i++) {
		printf("Current PID at index %d: %d\n", i, arr[i]);
	}
}

void removeFromPIDArr(pid_t* arr, pid_t el, int size) {
	int index = -1;
	for(int i = 0; i < size; i++) {
		if(arr[i] == el) {
			index = i;
			break;
		}
	}
	if(index == -1) {
		return;
	}
	for(int x = index; x < size - 1; x++) {
		arr[x] = arr[x+1];
	}

}

int checkClients(pid_t* clients, int size, int current_clients) {
	for(int i = 0; i < size; i++) {
		pid_t pid = clients[i];
		if(pid == 0) {
			continue;
		}
		int status;
		pid_t res = waitpid(pid, &status, WNOHANG);
		printf("Checking PID: %d | Result: %d\n", pid, res);
		if(res != 0) {
			printf("Purging PID: %d from clients\n", pid);
			removeFromPIDArr(clients, pid, size);
			current_clients--;
		}
	}
	return current_clients;
}

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	pid_t clients[NUM_USERS];
	int current_clients = 0;

	if (argc != 2) {
	    fprintf(stderr,"usage: server port\n");
	    exit(1);
	}
	
	char* port = argv[1];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}
		current_clients = checkClients(clients, NUM_USERS, current_clients);
		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		pid_t pid = fork();
		if(pid < 0) {
			printf("fork failed");
			exit(1);
		}
		if(pid == 0) {
			// In child process
			close(sockfd);
			runInteraction(new_fd);
			close(new_fd);
			exit(1);
		} else {
			// In parent process
			close(new_fd);
			printf("Created new process with pid: %d\n", pid);
			clients[current_clients] = pid;
			current_clients++;
			printPIDArr(clients, NUM_USERS);
		}
	}
	close(sockfd);
	return 0;
}



