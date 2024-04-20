#include "myqueue.h"
#include <stdlib.h>

node_t* head = NULL;
node_t* tail = NULL;

void enqueue(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    node_t* new_node = malloc(sizeof(node_t));
    new_node->args = args;
    new_node->header = header;
    new_node->packet = packet;
    new_node->next = NULL;

    if(tail == NULL) {
        head = new_node;
    } else {
        tail->next = new_node;
    }
    tail = new_node;
}

node_t* dequeue(){
    if(head == NULL) {
        return NULL;
    } else {
        node_t* result = head;
        node_t* temp = head;
        head = head->next;
        if (head == NULL) {
            tail = NULL;
        } 
        free(temp);
        return result;
    }
}