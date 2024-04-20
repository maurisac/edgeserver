#ifndef MYQUEUE_H_
#define MYQUEUE_H_

#include <pcap.h> 

struct node {
    struct node* next;
    u_char *args; 
    const struct pcap_pkthdr *header;
    const u_char *packet;
};

typedef struct node node_t;

void enqueue(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
node_t* dequeue();

#endif