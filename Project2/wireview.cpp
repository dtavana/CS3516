#include <iostream>
#include <string>
#include <pcap/pcap.h>
#include <vector>
#include <map>
#include <set>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

using namespace std;

map<string, int> unique_senders_macs;
map<string, int> unique_recipients_macs;
map<string, int> unique_senders_ips;
map<string, int> unique_recipients_ips;
map<string, string> arp_machines;
set<int> udp_source_ports;
set<int> udp_dest_ports;

u_int min_packet_length = 0;
u_int max_packet_length = 0;
u_int packet_length_sum = 0;

int seen_packets = 0;
struct timeval start_capture_time;
struct timeval end_capture_time;

string seperator = "--------------------------------";

string ethernet_address_to_string(uint8_t* ptr) {
    // Leave space for ':'
    char address_buf[ETH_ALEN * 3];
    sprintf(address_buf, 
            "%02x:%02x:%02x:%02x:%02x:%02x", 
            ptr[0], 
            ptr[1], 
            ptr[2],
            ptr[3],
            ptr[4],
            ptr[5]);
    string res(address_buf);
    return res;
}

string ipv4_adress_to_string(uint32_t* addr) {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, addr, buffer, INET_ADDRSTRLEN);
    string res (buffer);
    return res;
}

string time_to_string(timeval ts) {
    time_t time_seconds = ts.tv_sec;
    struct tm* to = localtime(&time_seconds);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%F %T", to);
    string res (time_buf);
    return res;
}

// Taken from https://stackoverflow.com/a/1858063
int timeval_subtract (struct timeval* result, struct timeval* x, struct timeval* y)
{
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
        tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

int update_unique_host_map(map<string, int>& unique_host_map, string key) {
    if(unique_host_map.count(key) > 0) {
        // Value exists, increment
        unique_host_map[key]++;
        return 1;
    } else {
        unique_host_map.insert(make_pair(key, 1));
        return 0;
    }
}

int update_arp_machines_map(string key, string value) {
    if(arp_machines.count(key) == 0) {
        arp_machines.insert(make_pair(key, value));
        return 0;
    }
    return 1;
}

void print_unique_host_map(map<string, int>& unique_host_map) {
    if(unique_host_map.size() == 0) {
        cout << "No entries" << endl;
    } else {
        for(auto const el : unique_host_map) {
        cout << "Host: " << el.first << " | Number of packets: " << el.second << endl;
        }
    }
}

void print_arp_machines_map() {
    if(arp_machines.size() == 0) {
        cout << "No entries" << endl;
    } else {
        for(auto const el : arp_machines) {
            cout << "MAC Address: " << el.first << " | IP Address: " << el.second << endl;
        }
    }
    
}

void print_port_set(set<int> port_set) {
    if(port_set.size() == 0) {
        cout << "No entries" << endl;
    } else {
        for (auto const el : port_set) {
            cout << el << endl;
        }
    }
}

void print_stats() {
    cout << seperator << endl;
    // Print capture start time
    cout << "Packet capture started at: " << time_to_string(start_capture_time) << endl;
    cout << seperator << endl;
    // Print total duration
    struct timeval* duration = (struct timeval*) malloc(sizeof(struct timeval));
    timeval_subtract(duration, &end_capture_time, &start_capture_time);
    cout << "Total elapsed time for packet capture: " << duration->tv_sec << "." << duration->tv_usec << " second(s)" << endl;
    cout << seperator << endl;
    free(duration);
    // Print total number of packets captured
    cout << "Total packets captured: " << seen_packets << endl;
    cout << seperator << endl;
    // Print unique host maps
    cout << "Unique Sender MAC Addresses:" << endl;
    print_unique_host_map(unique_senders_macs);
    cout << seperator << endl;
    cout << "Unique Sender IP Addresses:" << endl;
    print_unique_host_map(unique_senders_ips);
    cout << seperator << endl;
    cout << "Unique Recipient MAC Addresses:" << endl;
    print_unique_host_map(unique_recipients_macs);
    cout << seperator << endl;
    cout << "Unique Recipient IP Addresses:" << endl;
    print_unique_host_map(unique_recipients_ips);
    cout << seperator << endl;
    // Print ARP Machines Map
    cout << "Machines Participating in ARP:" << endl;
    print_arp_machines_map();
    cout << seperator << endl;
    // Print unique UDP ports
    cout << "Unique Source UDP Ports:" << endl;
    print_port_set(udp_source_ports);
    cout << seperator << endl;
    cout << "Unique Destination UDP Ports:" << endl;
    print_port_set(udp_dest_ports);
    cout << seperator << endl;
    // Print packet length statistics
    cout << "Minimum Packet Length: " << min_packet_length << endl;
    cout << seperator << endl;
    cout << "Maxmimum Packet Length: " << max_packet_length << endl;
    cout << seperator << endl;
    cout << "Average Packet Length: " << (packet_length_sum / seen_packets) << endl;
    cout << seperator << endl;
}

void pcap_callback(u_char* _, const struct pcap_pkthdr* header, const u_char* data) {
    if(seen_packets == 0) {
        // Save initial packet capture time
        start_capture_time = header->ts;
        // Save first packets len as initial min_packet_length
        min_packet_length = header->len;
    }

    // Increment seen_packets and save latest packet timestamp as final capture time
    seen_packets++;
    end_capture_time = header->ts;

    if(header->len > max_packet_length) {
        max_packet_length = header->len;
    } else if (header->len < min_packet_length) {
        min_packet_length = header->len;
    }
    packet_length_sum += header->len;

    // Ethernet header
    ether_header* ethernet_header = (ether_header*) data;
    string source_host = ethernet_address_to_string(ethernet_header->ether_shost);
    string dest_host = ethernet_address_to_string(ethernet_header->ether_dhost);
    update_unique_host_map(unique_senders_macs, source_host);
    update_unique_host_map(unique_recipients_macs, dest_host);

    // Skip over IPV6 Packets
    if(ntohs(ethernet_header->ether_type) == ETHERTYPE_IPV6) {
        return;
    }

    if(ntohs(ethernet_header->ether_type) == ETHERTYPE_IP) {
        // IP Packet
        iphdr* ip_header = (iphdr*) (data + sizeof(ether_header));
        string source_ip = ipv4_adress_to_string(&ip_header->saddr);
        string dest_ip = ipv4_adress_to_string(&ip_header->daddr);
        update_unique_host_map(unique_senders_ips, source_ip);
        update_unique_host_map(unique_recipients_ips, dest_ip);
        if(ip_header->protocol == IPPROTO_UDP) {
            udphdr* udp_header = (udphdr*) (data + sizeof(ether_header) + (ip_header->ihl * 4));
            udp_source_ports.insert(ntohs(udp_header->uh_sport));
            udp_dest_ports.insert(ntohs(udp_header->uh_dport));
        }
    } else if (ntohs(ethernet_header->ether_type) == ETHERTYPE_ARP) {
        // ARP Packet
        ether_arp* arp_header = (ether_arp*) (data + sizeof(ether_header));
        string source_host = ethernet_address_to_string(arp_header->arp_sha);
        string source_ip = ipv4_adress_to_string((uint32_t*) arp_header->arp_spa);
        update_arp_machines_map(source_host, source_ip);
        if(ntohs(arp_header->ea_hdr.ar_op) == ARPOP_REPLY) {
            // Only add destination information on replys
            string dest_host = ethernet_address_to_string(arp_header->arp_tha);
            string dest_ip = ipv4_adress_to_string((uint32_t*) arp_header->arp_tpa);
            update_arp_machines_map(dest_host, dest_ip);
        }
    }
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        cerr << "Missing filename parameter, usage: ./wireview FILENAME" << endl;
        return 1;
    }
    char* filename = argv[1];
    char err_buf[PCAP_ERRBUF_SIZE];
    pcap_t* open_res = pcap_open_offline(filename, err_buf);
    if(open_res == NULL) {
        cerr << "Error opening input file:" << endl;
        cerr << err_buf << endl;
        return 1;
    }
    if(pcap_datalink(open_res) != DLT_EN10MB) {
        // Data is not from Ethernet
        cerr << "Provided capture file is not from Ethernet" << endl;
        return 1;
    }
    pcap_loop(open_res, 0, pcap_callback, NULL);
    pcap_close(open_res);
    print_stats();

    return 0;
}

