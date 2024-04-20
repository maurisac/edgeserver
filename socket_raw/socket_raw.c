// Compilazione: gcc -D_BSD_SOURCE socket_raw.c myqueue.c -o socket_raw -lm -lpcap

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>  
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pcap.h> // si deve installare la libreria
#include <netinet/if_ether.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <stdint.h>
#include "myqueue.h"

#define ACCEPTED_CLIENTS 3
#define SERIES_LENGTH 3
#define BUFFER_SIZE 2500
#define ROWS_BEFORE_SENDING 200
#define SERVER_PORT 7237
#define DESTINATION_PORT 6011

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

char filename[] = "csv_files/sensor_signals.csv";

typedef struct rms_values{
    float x;
    float y;
    float z;
} rms_values;

typedef struct values_to_send{
    float x[ROWS_BEFORE_SENDING + 1];
    float y[ROWS_BEFORE_SENDING + 1];
    float z[ROWS_BEFORE_SENDING + 1];
} values_to_send;

typedef struct handle_client_arguments{
    u_char *args; 
    const struct pcap_pkthdr *header;
    const u_char *packet;
} handle_client_arguments;

void* send_to_server(void* args){
    values_to_send values = *((values_to_send*) args);
    int destination_socket, i, j, k;
    struct sockaddr_in destination_addr;
    char destination_ip[] = "127.0.0.1";
    float message[(ROWS_BEFORE_SENDING * SERIES_LENGTH) + SERIES_LENGTH];

    for (i = 0; i <= ROWS_BEFORE_SENDING; i++){
        message[i] = values.x[i];
    } 

    for (j = 0; j <= ROWS_BEFORE_SENDING; j++){
        message[j + i] = values.y[j];
    } 

    for (k = 0; k <= ROWS_BEFORE_SENDING; k++){
        message[k + j + i] = values.z[k];
    } 

    /*
    for(int l = 0; l < (ROWS_BEFORE_SENDING * 3) + 3; l++){
        printf("%f", message[l]);
    }
    */

    destination_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (destination_socket == -1) {
        printf("There was a problem while creating the destination socket..\n");
        pthread_exit(0);
    }

    destination_addr.sin_family = AF_INET;
    destination_addr.sin_port = htons(DESTINATION_PORT);
    destination_addr.sin_addr.s_addr = inet_addr(destination_ip);

    if (connect(destination_socket, (struct sockaddr*) &destination_addr, sizeof(destination_addr)) == -1) {
        printf("There was an error while connecting to the server..\n");
        pthread_exit(0);
    }

    printf("Sending data to the destination server..\n");
    if (send(destination_socket, message, sizeof(message), 0) == -1) {
        printf("There was an error while sending data to the server..\n%s\n", strerror(errno));
        pthread_exit(0);
    }
    printf("Data sent\n");
    close(destination_socket);
    pthread_exit(0);
}

rms_values root_mean_squared(values_to_send values){
    rms_values rms;

    rms.x = 0;
    rms.y = 0;
    rms.z = 0;

    for (int i = 0; i < ROWS_BEFORE_SENDING; i++){
        rms.x += pow(values.x[i], 2);
        rms.y += pow(values.y[i], 2);
        rms.z += pow(values.z[i], 2);
    }

    rms.x = sqrt(rms.x / ROWS_BEFORE_SENDING);
    rms.y = sqrt(rms.y / ROWS_BEFORE_SENDING);
    rms.z = sqrt(rms.z / ROWS_BEFORE_SENDING);

    return rms;
} 

void write_to_file(values_to_send values){
    FILE *fp;

    pthread_mutex_lock(&file_mutex);
    fp = fopen(filename, "a");
    if (fp != NULL) {
        fprintf(fp, "\n");
        for (int i = 0; i < ROWS_BEFORE_SENDING; i++){
            fprintf(fp, "X: %f, Y:%f, Z:%f\n", values.x[i], values.y[i], values.z[i]);
        }
        pthread_mutex_unlock(&file_mutex);
        fclose(fp);
    } else {
        printf("There was an error while opening the file: %s\n", strerror(errno));
        exit(1);
    }
}

void handle_client(void *args) {
    node_t* client = (node_t*) args;



    struct ether_header *eth_header;
    eth_header = (struct ether_header *) client->packet;
    
    if (ntohs(eth_header->ether_type) != ETHERTYPE_IP) {
        printf("Not an IP packet. Skipping...\n\n");
        return;
    }

    printf("Total packet available: %d bytes\n", client->header->caplen);
    printf("Expected packet size: %d bytes\n", client->header->len);

    const u_char *ip_header;
    const u_char *tcp_header;
    const u_char *payload;

    int ethernet_header_length = 14;
    int ip_header_length;
    int tcp_header_length;
    int payload_length;

    ip_header = client->packet + ethernet_header_length;
    ip_header_length = ((*ip_header) & 0x0F);
    ip_header_length = ip_header_length * 4;
    printf("IP header length (IHL) in bytes: %d\n", ip_header_length);

    u_char protocol = *(ip_header + 9);
    if (protocol != IPPROTO_TCP) {
        printf("Not a TCP packet. Skipping...\n\n");
        return;
    }   
    
    tcp_header = client->packet + ethernet_header_length + ip_header_length;
    tcp_header_length = ((*(tcp_header + 12)) & 0xF0) >> 4;
    tcp_header_length = tcp_header_length * 4;
    printf("TCP header length in bytes: %d\n", tcp_header_length);

    int total_headers_size = ethernet_header_length+ip_header_length+tcp_header_length;
    printf("Size of all headers combined: %d bytes\n", total_headers_size);
    payload_length = client->header->caplen - total_headers_size;
    printf("Payload size: %d bytes\n", payload_length);
    payload = client->packet + total_headers_size;
    printf("Memory address where payload begins: %p\n\n", payload);
    
    ssize_t bytes_received;
    values_to_send values;
    printf("Handling client..\n");


    int offset = 0, index = 0;
    while (offset < payload_length) {
        for (int i = 0; i < SERIES_LENGTH; i++) {
            pthread_t write_thread;
            float value = *((float*)(payload + offset));
            switch (i){
            case 0:
                values.x[index] = value;
                break;
            case 1:
                values.y[index] = value;
                break;  
            case 2:
                values.z[index] = value;
                break;
            }
            offset += sizeof(float);
        }
        index++;
    }
    
    pthread_t thread;
    rms_values rms = root_mean_squared(values);
    values.x[ROWS_BEFORE_SENDING] = rms.x;
    values.y[ROWS_BEFORE_SENDING] = rms.y;
    values.z[ROWS_BEFORE_SENDING] = rms.z;

    pthread_create(&thread, NULL, send_to_server, (void*) &values);
    pthread_join(thread, NULL);
    write_to_file(values);

    printf("Client handled..\n");
    return;
}

void* threads_function(void* args){
    while(1) {
        pthread_mutex_lock(&queue_mutex);
        node_t* client = dequeue();
        pthread_mutex_unlock(&queue_mutex);
        
        if (client != NULL) {
            handle_client((void*) client);
        } else {
            pthread_mutex_lock(&queue_mutex);
            pthread_cond_wait(&cond, &queue_mutex);
            pthread_mutex_unlock(&queue_mutex);
        }
    }
}

void queue_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet){
    pthread_mutex_lock(&queue_mutex);
    enqueue(args, header, packet);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&queue_mutex);
}

int main(int argc, char* argv[]) {
    int snapshot_length = 262144;
    struct stat st = {0};
    char error_buffer[PCAP_ERRBUF_SIZE], *device = "lo";
    pcap_t *handle;
    struct bpf_program filter;
    char filter_exp[] = "tcp port 7237";

    if (stat("./csv_files", &st) == -1) {
        mkdir("./csv_files", 0700);
    }

    pthread_t thread_ids[ACCEPTED_CLIENTS];

    for(int i = 0; i < ACCEPTED_CLIENTS; i++){
        pthread_create(&thread_ids[i], NULL, threads_function, NULL);
    }

    handle = pcap_open_live(device, snapshot_length, 1, 10000, error_buffer);
    if (handle == NULL) {
        fprintf(stderr, "Error opening device: %s\n", error_buffer);
        return 1;
    }

    if (pcap_compile(handle, &filter, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
        return 1;
    }

    if (pcap_setfilter(handle, &filter) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
        return 1;
    }

    while(1) {
        pcap_loop(handle, 0, queue_packet, NULL);
    }
    
    pcap_close(handle);
    return 0;
}
