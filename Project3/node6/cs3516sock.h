#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

// Set the following port to a unique number:
#define MYPORT 5950

int create_cs3516_socket(in_addr_t ip) {
    int sock;
    struct sockaddr_in server;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) perror("Error creating CS3516 socket");

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = ip;
    server.sin_port = htons(MYPORT);
    if (bind(sock, (struct sockaddr *) &server, sizeof(server) ) < 0) 
        perror("Unable to bind CS3516 socket");

    // Socket is now bound:
    return sock;
}

int cs3516_recv(int sock, void* buffer, int buff_size) {
    struct sockaddr_in from;
    int n;
    socklen_t fromlen;
    fromlen = sizeof(struct sockaddr_in);
    n = recvfrom(sock, buffer, buff_size, 0,
                 (struct sockaddr *) &from, &fromlen);

    return n;
}

int cs3516_send(int sock, void* buffer, int buff_size, unsigned long nextIP) {
    struct sockaddr_in to;
    int tolen, n;

    tolen = sizeof(struct sockaddr_in);

    // Okay, we must populate the to structure. 
    bzero(&to, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(MYPORT);
    to.sin_addr.s_addr = nextIP;

    // We can now send to this destination:
    n = sendto(sock, buffer, buff_size, 0,
               (struct sockaddr *)&to, tolen);

    return n;
}
