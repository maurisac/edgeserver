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

#define ACCEPTED_CLIENTS 3
#define SERIES_LENGTH 3
#define BUFFER_SIZE 2500
#define ROWS_BEFORE_SENDING 200
#define SERVER_PORT 7238
#define DESTINATION_PORT 6015

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

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

rms_values root_mean_square(values_to_send values){
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
        pthread_mutex_unlock(&file_mutex);
        exit(1);
    }
}

void* handle_client(void *args) {
    int client_sock = *((int*)args);
    char client_message[BUFFER_SIZE];
    ssize_t bytes_received;
    values_to_send values;
    printf("Handling client..\n");
    memset(client_message, '\0', sizeof(client_message));

    bytes_received = recv(client_sock, client_message, sizeof(client_message), MSG_WAITALL);
    if (bytes_received < 0) {
        printf("There was an error while receiving the data.\n");
        pthread_exit(0);
    }

    int offset = 0, index = 0;
    while (offset < bytes_received) {
        for (int i = 0; i < SERIES_LENGTH; i++) {
            float value = *((float*)(client_message + offset));
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
    rms_values rms = root_mean_square(values);
    values.x[ROWS_BEFORE_SENDING] = rms.x;
    values.y[ROWS_BEFORE_SENDING] = rms.y;
    values.z[ROWS_BEFORE_SENDING] = rms.z;

    pthread_create(&thread, NULL, send_to_server, (void*) &values);
    write_to_file(values);
    pthread_join(thread, NULL);
    
    close(client_sock);
    printf("Client handled..\n");
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    int server_socket;
    struct sockaddr_in server_addr;
    struct stat st = {0};
    char server_ip[] = "127.0.0.1";

    if (stat("./csv_files", &st) == -1) {
        mkdir("./csv_files", 0700);
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket == -1) {
        printf("There was an error while opening the socket.\n");
        exit(0);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    bind(server_socket, (struct sockaddr*) &server_addr, sizeof(server_addr));

    if (listen(server_socket, ACCEPTED_CLIENTS) != -1) {
        while (1) {
            pthread_t client_thread;
            struct sockaddr_in client_addr;
            int *client_sock = (int*)malloc(sizeof(int));
            int client_size = sizeof(client_addr);


            *client_sock = accept(server_socket, (struct sockaddr*) &client_addr, &client_size);
            if (*client_sock != -1){
                printf("IP address is: %s\n", inet_ntoa(client_addr.sin_addr));
                printf("port is: %d\n", (int) ntohs(client_addr.sin_port));

                pthread_create(&client_thread, NULL, handle_client, (void*) client_sock);
                pthread_join(client_thread, NULL);
                free(client_sock);
            } else {
                printf("There was an error while accepting the client.\n");
                exit(0);
            }
        }
    }
    return 0;
}