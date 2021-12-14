#include <iostream>
using namespace std;
#include "cs3516sock.h"
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>
#include <queue>
#include <map>
#include <vector>
#include <bitset>
#include <fstream>
 
#define ETHERNET_HEADER_SIZE 14
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
 
#define MAX_RECEIVE_BUFFER_SIZE 1000
#define MAX_LOG_ENTRY_SIZE 500
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

struct QueueNode
{
    char** buffer;
    uint32_t fileSize;
    time_t timestamp;
    string destAddress;
};
struct TrieNode {
    uint32_t nextHopIP;
    bool isSet;
    struct TrieNode* zeroChild;
    struct TrieNode* oneChild;
};
struct NodeData {
    int nodeId;
    uint32_t realNetworkIp;
    uint32_t overlayIp;
    bool isRouter;
};
 
int sock;
int currentId = 0;
int queueLength;
int defaultTtlValue;
NodeData* myConfig;
NodeData* outRouter;

map<uint32_t, vector<QueueNode*>> outputQueues;
TrieNode* root;

map<int, NodeData*> nodeData;
map<int, int> outRouterData;
map<uint32_t, map<uint32_t, int>> delayData;

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

void addNodeToTrie(string prefix, string nextHop) {
    string ip = strtok((char*) prefix.c_str(), "/");
    int prefixLength = atoi(strtok(NULL, "/"));
    int firstSegment = atoi(strtok((char*) ip.c_str(), "."));
    int secondSegment = atoi(strtok(NULL, "."));
    int thirdSegment = atoi(strtok(NULL, "."));
    int fourthSegment = atoi(strtok(NULL, "."));
    bitset<8> binFirstSegment(firstSegment);
    bitset<8> binSecondSegment(secondSegment);
    bitset<8> binThirdSegment(thirdSegment);
    bitset<8> binFourthSegment(fourthSegment);
    bitset<32> totalBin(binFourthSegment.to_string() + binThirdSegment.to_string() + binSecondSegment.to_string() + binFirstSegment.to_string());
    TrieNode* currentNode = root;
    for(int i = 0; i < min(prefixLength, 32); ++i) {
        int currentBit = totalBin[i];
        if(currentBit == 0) {
            if(currentNode->zeroChild == NULL) {
                currentNode->zeroChild = (TrieNode*) malloc(sizeof(TrieNode));
                currentNode->zeroChild->isSet = 0;
                currentNode->zeroChild->nextHopIP = 0;
                currentNode->zeroChild->zeroChild = NULL;
                currentNode->zeroChild->oneChild = NULL;
                currentNode = currentNode->zeroChild;
                continue; 
            } else {
                currentNode = currentNode->zeroChild;
                continue; 
            }
        } else {
            if(currentNode->oneChild == NULL) {
                currentNode->oneChild = (TrieNode*) malloc(sizeof(TrieNode));
                currentNode->oneChild->isSet = 0;
                currentNode->oneChild->nextHopIP = 0;
                currentNode->oneChild->zeroChild = NULL;
                currentNode->oneChild->oneChild = NULL;
                currentNode = currentNode->oneChild;
                continue; 
            } else {
                currentNode = currentNode->oneChild;
                continue; 
            }
        }
    }
    currentNode->isSet = 1;
    currentNode->nextHopIP = stringIpToByte(nextHop);
}

uint32_t searchForNodeInTrie(string ip) {
    int firstSegment = atoi(strtok((char*) ip.c_str(), "."));
    int secondSegment = atoi(strtok(NULL, "."));
    int thirdSegment = atoi(strtok(NULL, "."));
    int fourthSegment = atoi(strtok(NULL, "."));
    bitset<8> binFirstSegment(firstSegment);
    bitset<8> binSecondSegment(secondSegment);
    bitset<8> binThirdSegment(thirdSegment);
    bitset<8> binFourthSegment(fourthSegment);
    bitset<32> totalBin(binFourthSegment.to_string() + binThirdSegment.to_string() + binSecondSegment.to_string() + binFirstSegment.to_string());
    
    TrieNode* currentNode = root;
    uint32_t lastIpSeen;
    for(int i = 0; i < 32; ++i) {
        int currentBit = totalBin[i];
        if(currentNode->isSet) {
            lastIpSeen = currentNode->nextHopIP;
        }
        if(currentBit == 0) {
            if(currentNode->zeroChild == NULL) {
                return lastIpSeen;
            } else {
                currentNode = currentNode->zeroChild;
                continue; 
            }
        } else {
            if(currentNode->oneChild == NULL) {
                return lastIpSeen;
            } else {
                currentNode = currentNode->oneChild;
                continue; 
            }
        }
    }
    return 0;
}
 
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
 
int getBufferSize(char* payload) {
    return IP_HEADER_SIZE + UDP_HEADER_SIZE + strlen(payload);
}
 
void createOverlayHeaders(char* buffer, char* data, string destAddress) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    struct udphdr* udpHeader = (struct udphdr*) (buffer + IP_HEADER_SIZE);
    char* payload = (buffer + IP_HEADER_SIZE + UDP_HEADER_SIZE);
    strncpy(payload, data, strlen(data));
    ipHeader->saddr = myConfig->overlayIp;
    ipHeader->daddr = stringIpToByte(destAddress);
    ipHeader->tos = 0;
    ipHeader->frag_off = 0;
    ipHeader->check = 0;
    //ipHeader->tot_len = ?;
    ipHeader->ihl = 5;
    ipHeader->protocol = IPPROTO_UDP; // 17
    ipHeader->ttl = defaultTtlValue;
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
    cs3516_send(sock, &payloadSize, sizeof(uint32_t), outRouter->realNetworkIp);
    cs3516_send(sock, buffer, bufferSize, outRouter->realNetworkIp);
    currentId++;
}

int getDelay(char* buffer) {
    struct iphdr* ipHeader = (struct iphdr*) buffer;
    map<uint32_t, map<uint32_t, int>>::iterator allDelayMappings = delayData.find(myConfig->realNetworkIp);
    return allDelayMappings->second.find(ipHeader->daddr)->second;
}
 
void sendData() {
    for(auto const& el : outputQueues) {
        vector<QueueNode*> q = el.second;
        vector<QueueNode*> newQueue;
        for(auto const queueEl : q) {
            time_t currentTimestamp = time(NULL) * 1000;
            char* buffer = *queueEl->buffer;
            int delay = getDelay(buffer);
            if(queueEl->timestamp + delay < currentTimestamp) {
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

uint32_t realIpFromOverlayIp(uint32_t overlayIp) {
    for(auto const el : nodeData) {
        if(el.second->overlayIp == overlayIp) {
            return el.second->realNetworkIp;
        }
    }
    return 0;
}
 
void enqueueData(char** buffer, uint32_t fileSize) {
    struct iphdr* ipHeader = (struct iphdr*) *buffer;
    uint32_t realNetworkIp = realIpFromOverlayIp(ipHeader->daddr);
    map<uint32_t, vector<QueueNode*>>::iterator directIt = outputQueues.find(realNetworkIp);
    if(directIt != outputQueues.end()) {
        // Direct connection to output, forward it
        vector<QueueNode*> queue = directIt->second;
        QueueNode* node = (QueueNode*) malloc(sizeof(QueueNode));
        node->buffer = buffer;
        node->fileSize = fileSize;
        node->timestamp = time(NULL) * 1000;
        string stringIp = byteIpToString(&realNetworkIp);
        node->destAddress = stringIp;
        queue.push_back(node);
        outputQueues[realNetworkIp].swap(queue);
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, SENT_OKAY, byteIpToString(&ipHeader->daddr));
        return;
    }
    uint32_t byteIp = searchForNodeInTrie(byteIpToString(&ipHeader->daddr));
    string stringIp = byteIpToString(&byteIp);
    map<uint32_t, vector<QueueNode*>>::iterator it = outputQueues.find(byteIp);
    if(it == outputQueues.end()) {
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, NO_ROUTE_TO_HOST);
        return;
    }
    vector<QueueNode*> queue = it->second;
    if(queue.size() >= queueLength) {
        // Drop-Tail Queueing
        addRouterLogEntry(byteIpToString(&ipHeader->saddr), byteIpToString(&ipHeader->daddr), ipHeader->id, MAX_SENDQ_EXCEEDED);
        return;
    }
    QueueNode* node = (QueueNode*) malloc(sizeof(QueueNode));
    node->buffer = buffer;
    node->fileSize = fileSize;
    node->timestamp = time(NULL) * 1000;
    node->destAddress = stringIp;
    queue.push_back(node);
    outputQueues[byteIp].swap(queue);
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

void insertDelayData(int firstNodeId, int firstNodeDelay, int secondNodeId, int secondNodeDelay) {
    map<uint32_t, map<uint32_t, int>>::iterator delayMapping;
    map<int, NodeData*>::iterator firstNodeDataIt = nodeData.find(firstNodeId);
    map<int, NodeData*>::iterator secondNodeDataIt = nodeData.find(secondNodeId);
    delayMapping = delayData.find(firstNodeDataIt->second->realNetworkIp);
    if(delayMapping == delayData.end()) {
        map<uint32_t, int> newMap;
        newMap.insert(make_pair(secondNodeDataIt->second->realNetworkIp, firstNodeDelay));
        delayData.insert(make_pair(firstNodeDataIt->second->realNetworkIp, newMap));
    } else {
        delayData[firstNodeDataIt->second->realNetworkIp].insert(make_pair(secondNodeDataIt->second->realNetworkIp, firstNodeDelay));
    }
    delayMapping = delayData.find(secondNodeDataIt->second->realNetworkIp);
    if(delayMapping == delayData.end()) {
        map<uint32_t, int> newMap;
        newMap.insert(make_pair(firstNodeDataIt->second->realNetworkIp, secondNodeDelay));
        delayData.insert(make_pair(secondNodeDataIt->second->realNetworkIp, newMap));
    } else {
        delayData[secondNodeDataIt->second->realNetworkIp].insert(make_pair(firstNodeDataIt->second->realNetworkIp, secondNodeDelay));
    }
}

void readConfigFile(string filename) {
    ifstream fp(filename);
    if(!fp.is_open()) {
        cerr << "Could not open " << filename << endl;
        close(sock);
        exit(1);
    }

    string line;
    int nodeId;
    string realNetworkIp;
    string overlayIp;
    NodeData* data;
    int firstNodeId;
    int firstNodeDelay;
    int secondNodeId;
    int secondNodeDelay;
    string prefix;

    while(getline(fp, line)) {
        int lineType = atoi(strtok((char*) line.c_str(), " "));
        switch (lineType) {
            case 0:
                queueLength = atoi(strtok(NULL, " "));
                defaultTtlValue = atoi(strtok(NULL, " "));
                break;
            case 1:
                nodeId = atoi(strtok(NULL, " "));
                realNetworkIp = strtok(NULL, " ");
                data = (NodeData*) malloc(sizeof(NodeData));
                data->nodeId = nodeId;
                data->realNetworkIp = stringIpToByte(realNetworkIp);
                data->isRouter = true;
                nodeData.insert(make_pair(nodeId, data));
                break;
            case 2:
                nodeId = atoi(strtok(NULL, " "));
                realNetworkIp = strtok(NULL, " ");
                overlayIp = strtok(NULL, " ");
                data = (NodeData*) malloc(sizeof(NodeData));
                data->nodeId = nodeId;
                data->realNetworkIp = stringIpToByte(realNetworkIp);
                data->overlayIp = stringIpToByte(overlayIp);
                data->isRouter = false;
                nodeData.insert(make_pair(nodeId, data));
                break;
            case 3:
                firstNodeId = atoi(strtok(NULL, " "));
                firstNodeDelay = atoi(strtok(NULL, " "));
                secondNodeId = atoi(strtok(NULL, " "));
                secondNodeDelay = atoi(strtok(NULL, " "));
                insertDelayData(firstNodeId, firstNodeDelay, secondNodeId, secondNodeDelay);
                break;
            case 4:
                firstNodeId = atoi(strtok(NULL, " "));
                firstNodeDelay = atoi(strtok(NULL, " "));
                prefix = strtok(NULL, " ");
                secondNodeId = atoi(strtok(NULL, " "));
                secondNodeDelay = atoi(strtok(NULL, " "));
                addNodeToTrie(prefix, byteIpToString(&nodeData.find(firstNodeId)->second->realNetworkIp));
                insertDelayData(firstNodeId, firstNodeDelay, secondNodeId, secondNodeDelay);
                outRouterData.insert(make_pair(secondNodeId, firstNodeId));
                break;
        }
    }
    fp.close();
}

void setupOutputQueues() {
    map<uint32_t, map<uint32_t, int>>::iterator allDelayMappings = delayData.find(myConfig->realNetworkIp);
    for(auto const el : allDelayMappings->second) {
        vector<QueueNode*> v;
        outputQueues.insert(make_pair(el.first, v));
    }
}
 
void runRouter() {
    cout << "I am a router" << endl;
    sock = create_cs3516_socket(myConfig->realNetworkIp);
    setupOutputQueues();
    while(1) {
        recvNonBlocking(1);
    }
}
 
void runHost() {
    if(myConfig->overlayIp == stringIpToByte("1.2.3.1")) {
        cout << "I am a sending host" << endl;
        sock = create_cs3516_socket(myConfig->realNetworkIp);
        char payload[12] = {"hello world"};
        sendInitialData(payload, 12, "4.5.6.1");
    } else {
        cout << "I am a host" << endl;
        sock = create_cs3516_socket(myConfig->realNetworkIp);
    }
    while(1) {
        recvNonBlocking(0);
    }
 
}
 
int main(int argc, char* argv[]) {
    if(argc > 2) {
        int nodeId = atoi(argv[1]);
        string filename = argv[2];
        root = (TrieNode*) malloc(sizeof(TrieNode));
        root->isSet = 0;
        root->nextHopIP = 0;
        root->zeroChild = NULL;
        root->oneChild = NULL;
        myConfig = (NodeData*) malloc(sizeof(NodeData));
        outRouter = (NodeData*) malloc(sizeof(NodeData));
        readConfigFile(filename);
        map<int, NodeData*>::iterator dataIt = nodeData.find(nodeId);
        if(dataIt == nodeData.end()) {
            cerr << "Invalid node id passed in" << endl;
            exit(1);
        }
        myConfig->nodeId = dataIt->second->nodeId;
        myConfig->realNetworkIp = dataIt->second->realNetworkIp;
        myConfig->overlayIp = dataIt->second->overlayIp;
        myConfig->isRouter = dataIt->second->isRouter;
        if(!myConfig->isRouter) {
            int outRouterId = outRouterData.find(nodeId)->second;
            outRouter->nodeId = outRouterId;
            outRouter->realNetworkIp = nodeData.find(outRouterId)->second->realNetworkIp;
            outRouter->isRouter = true;
            runHost();
        } else {
            runRouter();
        }
    } else {
        // NO PARAMETERS
        cerr << "Usage ./project3 NODEID CONFIG_FILENAME" << endl;
        exit(1);
    }
}