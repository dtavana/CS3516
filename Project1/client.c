/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define MAXDATASIZE 100 // max number of bytes we can get at once 

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_file(char* filename, int sockfd) {
	FILE* fp;

	fp = fopen(filename, "rb");
	if(fp == NULL) {
		perror("Could not open the specified file");
		exit(1);
	}

	// Read file
	char data[4096];
	uint32_t filesize = fread(data, 1, 4096, fp);
	
	printf("Read file of size: %d\n", filesize);
	printf("Read file with data: %s\n", data);

	// Send file size
	if(send(sockfd, &filesize, sizeof(uint32_t), 0) == -1) {
		perror("send size");
	}
	printf("Done sending file size\n");

	// Send file contents
	if(send(sockfd, data, sizeof(data), 0) == -1) {
		perror("send contents");
	}
	printf("Done sending file contents\n");
	fclose(fp);
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	if (argc != 4) {
	    fprintf(stderr,"usage: client hostname filename port\n");
	    exit(1);
	}

	char* host = argv[1];
	char* filename = argv[2];
	char* port = argv[3];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	send_file(filename, sockfd);

	close(sockfd);

	return 0;
}

