#include <iostream>
using namespace std;
#include "cs3516sock.h"
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>

#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8

#define MAX_RECEIVE_BUFFER_SIZE 3000

unsigned long stringIpToByte(string ip) {
    struct sockaddr_in to;
    inet_aton(ip.c_str(), &to.sin_addr);
    return to.sin_addr.s_addr;
}

string byteIpToString(uint32_t* addr) {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, addr, buffer, INET_ADDRSTRLEN);
    string res (buffer);
    return res;
}

int getBufferSize(char* payload) {
    return IP_HEADER_SIZE + UDP_HEADER_SIZE + strlen(payload);
}

void createOverlayHeaders(char* buffer, char* data, string destAddress) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    struct udphdr* udpHeader = (struct udphdr*) (buffer + IP_HEADER_SIZE);
    char* payload = (buffer + IP_HEADER_SIZE + UDP_HEADER_SIZE);
    strncpy(payload, data, strlen(data));
    ipHeader->daddr = stringIpToByte(destAddress);
    udpHeader->uh_dport = MYPORT;
}

void printOverlayHeader(char* buffer) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    struct udphdr* udpHeader = (struct udphdr*) (buffer + IP_HEADER_SIZE);
    char* payload = (buffer + IP_HEADER_SIZE + UDP_HEADER_SIZE);
    cout << byteIpToString(&ipHeader->daddr) << endl;
    cout << udpHeader->uh_dport << endl;
    cout << payload << endl;
}

void sendData(int sock, char* payload, string destAddr) {
    int bufferSize = getBufferSize(payload);
    char buffer[bufferSize];
    createOverlayHeaders(buffer, payload, "127.0.0.1");
    printOverlayHeader(buffer);
    cs3516_send(sock, buffer, bufferSize, stringIpToByte("127.0.0.1"));
}

void recvData(int sock){
    char buffer[MAX_RECEIVE_BUFFER_SIZE];
    int recv = cs3516_recv(sock, buffer, MAX_RECEIVE_BUFFER_SIZE);
    cout << recv << endl;
    printOverlayHeader(buffer);
}

int main(int argc, char* argv[]) {
    if(argc > 1) {
        cout << "SERVER" << endl;
        int sock = create_cs3516_socket();
        recvData(sock);
    } else {
        cout << "CLIENT" << endl;
        char payload[12] = {"hello world"};
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sendData(sock, payload, "127.0.0.1");
        close(sock);
    }
}