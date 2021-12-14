#include <iostream>
using namespace std;
#include "cs3516sock.h"
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>
#include <queue>
#include <map>
#include <vector>
 
#define ETHERNET_HEADER_SIZE 14
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
 
#define MAX_RECEIVE_BUFFER_SIZE 1000
#define MAX_LOG_ENTRY_SIZE 500
#define MAX_QUEUE_LENGTH 2
#define ROUTER_DELAY 2000
 
enum status {
    TTL_EXPIRED,
    MAX_SENDQ_EXCEEDED,
    NO_ROUTE_TO_HOST,
    SENT_OKAY
};
string status_label[] = {
    "TTL_EXPIRED",
    "MAX_SENDQ_EXCEEDED",
    "NO_ROUTE_TO_HOST",
    "SENT_OKAY"
};
 
int sock;
int currentId = 0;
 
struct QueueNode
{
    char** buffer;
    uint32_t fileSize;
    time_t timestamp;
    string destAddress;
};
 
map<string, vector<QueueNode*>> outputQueues;
 
void addRouterLogEntry(string sourceIp, string destIp, int identifier, status statusCode, string nextHop = "") {
    char logEntry[MAX_LOG_ENTRY_SIZE];
    time_t timeSinceEpoch = time(NULL);
    sprintf(logEntry, 
            "%ld %s %s %d %s %s\n", 
            timeSinceEpoch, 
            sourceIp.c_str(),
            destIp.c_str(),
            identifier,
            status_label[statusCode].c_str(),
            nextHop.c_str());
    FILE* fp;
    fp = fopen("ROUTER_control.txt", "a");
    fputs(logEntry, fp);
    fclose(fp);
}
 
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
    //ipHeader->saddr = ?;
    ipHeader->saddr = stringIpToByte("10.0.2.15");
    ipHeader->daddr = stringIpToByte(destAddress);
    ipHeader->tos = 0;
    ipHeader->frag_off = 0;
    ipHeader->check = 0;
    //ipHeader->tot_len = ?;
    ipHeader->ihl = 5;
    ipHeader->protocol = IPPROTO_UDP; // 17
    //ipHeader->ttl = ?;
    ipHeader->ttl = 2;
    ipHeader->id = currentId;
 
    udpHeader->uh_sport = MYPORT;
    udpHeader->uh_dport = MYPORT;
    //udpHeader->uh_ulen = ?;
}
 
void printOverlayHeader(char* buffer) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    struct udphdr* udpHeader = (struct udphdr*) (buffer + IP_HEADER_SIZE);
    char* payload = (buffer + IP_HEADER_SIZE + UDP_HEADER_SIZE);
    cout << byteIpToString(&ipHeader->daddr) << endl;
    cout << udpHeader->uh_dport << endl;
    cout << payload << endl;
}
 
// Return 1 if packet should be dropped, 0 otherwise
int updateOverlayHeaders(char* buffer) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    ipHeader->ttl--;
    if(ipHeader->ttl <= 0) {
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, TTL_EXPIRED);
        return 1;
    }
    return 0;
 
}
 
void sendInitialData(char* payload, uint32_t payloadSize, string destAddr) {
    int bufferSize = getBufferSize(payload);
    char buffer[bufferSize];
    createOverlayHeaders(buffer, payload, destAddr);
    cs3516_send(sock, &payloadSize, sizeof(uint32_t), stringIpToByte("10.0.2.16"));
    cs3516_send(sock, buffer, bufferSize, stringIpToByte("10.0.2.16"));
    currentId++;
}
 
void sendData() {
    for(auto const& el : outputQueues) {
        vector<QueueNode*> q = el.second;
        vector<QueueNode*> newQueue;
        for(auto const queueEl : q) {
            time_t currentTimestamp = time(NULL) * 1000;
            if(queueEl->timestamp + ROUTER_DELAY < currentTimestamp) {
                char* buffer = *queueEl->buffer;
                cs3516_send(sock, &queueEl->fileSize, sizeof(uint32_t), stringIpToByte(queueEl->destAddress));
                cs3516_send(sock, buffer, IP_HEADER_SIZE + UDP_HEADER_SIZE + queueEl->fileSize, stringIpToByte(queueEl->destAddress));
            } else {
                newQueue.push_back(queueEl);
            }
        }
        outputQueues[el.first].swap(newQueue);
    }
}
 
void enqueueData(char** buffer, uint32_t fileSize) {
    struct iphdr* ipHeader = (struct iphdr*) *buffer;
    string ipAddress = byteIpToString(&ipHeader->daddr);
    map<string, vector<QueueNode*>>::iterator it = outputQueues.find(ipAddress);
    if(it == outputQueues.end()) {
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, NO_ROUTE_TO_HOST);
        return;
    }
    vector<QueueNode*> queue = it->second;
    if(queue.size() >= MAX_QUEUE_LENGTH) {
        // Drop-Tail Queueing
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, MAX_SENDQ_EXCEEDED);
        return;
    }
    QueueNode* node = (QueueNode*) malloc(sizeof(QueueNode));
    node->buffer = buffer;
    node->fileSize = fileSize;
    node->timestamp = time(NULL) * 1000;
    node->destAddress = ipAddress;
    queue.push_back(node);
    outputQueues[ipAddress].swap(queue);
    addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, SENT_OKAY, byteIpToString(&ipHeader->daddr));
}
 
void recvDataHost() {
    uint32_t fileSize;
    cs3516_recv(sock, &fileSize, sizeof(uint32_t));
    char buffer[IP_HEADER_SIZE + UDP_HEADER_SIZE + fileSize];
    cs3516_recv(sock, buffer, IP_HEADER_SIZE + UDP_HEADER_SIZE + fileSize);
    cout << "Received Data" << endl;
    buffer[IP_HEADER_SIZE + UDP_HEADER_SIZE + fileSize - 1] = '\0';
    printOverlayHeader(buffer);
}
 
void recvDataRouter(){
    uint32_t fileSize;
    cs3516_recv(sock, &fileSize, sizeof(uint32_t));
    char* buffer = (char*) malloc(IP_HEADER_SIZE + UDP_HEADER_SIZE + fileSize);
    cs3516_recv(sock, buffer, IP_HEADER_SIZE + UDP_HEADER_SIZE + fileSize);
    int ttlRes = updateOverlayHeaders(buffer);
    if(ttlRes != 1) {
        // If ttlRes is 1, drop packet
        enqueueData(&buffer, fileSize);
    }
 
}
 
void recvNonBlocking(int isRouter) {
    struct timeval tv;
    fd_set readfds;
 
    tv.tv_sec = 2;
    tv.tv_usec = 500000;
 
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
 
    select(sock + 1, &readfds, NULL, NULL, &tv);
 
    if (FD_ISSET(sock, &readfds)) {
        if(isRouter) {
            recvDataRouter();
        } else {
            recvDataHost();
        }
    }
 
    if(isRouter) {
        sendData();
    }
}
 
void runRouter() {
    cout << "I am a router" << endl;
    sock = create_cs3516_socket(stringIpToByte("10.0.2.16"));
    vector<QueueNode*> v;
    outputQueues.insert(make_pair("10.0.2.17", v));
    while(1) {
        recvNonBlocking(1);
    }
}
 
void runHost(int isSendingHost) {
    if(isSendingHost) {
        cout << "I am a sending host" << endl;
        sock = create_cs3516_socket(stringIpToByte("10.0.2.15"));
        char payload[12] = {"hello world"};
        sendInitialData(payload, 12, "10.0.2.17");
    } else {
        cout << "I am a host" << endl;
        sock = create_cs3516_socket(stringIpToByte("10.0.2.17"));
    }
    while(1) {
        recvNonBlocking(0);
    }
 
}
 
// Return 2 for router, 1 for sending host, 0 for regular host, -1 for invalid
int isRouter(char* argv[]) {
    if(strcmp(argv[1], "s") == 0){
        return 1;
    }
    if(strcmp(argv[1], "r") == 0){
        return 2;
    }
    if(strcmp(argv[1], "h") == 0){
        return 0;
    }
    return -1;
}
 
int main(int argc, char* argv[]) {
    if(argc > 1) {
        int isRouterRes = isRouter(argv);
        if(isRouterRes == 1 || isRouterRes == 0) {
            runHost(isRouterRes);
        } else if(isRouterRes == 2) {
            runRouter();
        } else {
            cerr << "Did not pass in 's', 'r' or 'h'" << endl;
            exit(1);
        }
    } else {
        // NO PARAMETERS
        cerr << "Usage ./project3 [s/r/h]" << endl;
        exit(1);
    }
}