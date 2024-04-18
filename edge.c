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

#define ACCEPTED_CLIENTS 3
#define SERIES_LENGTH 3
#define BUFFER_SIZE 10000
#define ROWS_BEFORE_RMS 20
#define ROWS_BEFORE_SENDING 100
#define SERVER_PORT 7156
#define DESTINATION_PORT 6013

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rms_mutex = PTHREAD_MUTEX_INITIALIZER;

int rms_rows_printed = 0, rms_last_read_line = 0, signals_rows_printed = 0, signals_last_read_line = 0;
char filename[] = "csv_files/sensor_signals.csv";

typedef struct rms_values{
    float x;
    float y;
    float z;
} rms_values;

typedef struct char_to_print{
    float value;
    int axis;
} char_to_print;

void send_signals_to_server(){
    FILE *fp;
    int destination_socket, axis = 0, current_row = 0, c = EOF, linecount = 0;
    struct sockaddr_in destination_addr;
    char destination_ip[] = "127.0.0.1", row[BUFFER_SIZE], *token;
    float signals[ROWS_BEFORE_SENDING][SERIES_LENGTH];

    destination_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (destination_socket == -1) {
        printf("There was a problem while creating the destination socket..\n");
        exit(0);
    }

    destination_addr.sin_family = AF_INET;
    destination_addr.sin_port = htons(DESTINATION_PORT);
    destination_addr.sin_addr.s_addr = inet_addr(destination_ip);

    fp = fopen(filename, "r");
    if (fp != NULL) {
        while (linecount < signals_last_read_line && (c=fgetc(fp)) != EOF) {
            if (c == '\n')
                linecount++;
        }
        while (fgets(row, BUFFER_SIZE, fp) != NULL && current_row < signals_rows_printed) {
            token = strtok(row, ",");

            while(token != NULL){
                float value = atof(token);
                signals[current_row][axis] = value;
                axis = (axis + 1) %  SERIES_LENGTH;
                token = strtok(NULL, ",");
            }
            current_row++;
            signals_last_read_line++;
        }
        signals_rows_printed = 0;

        fseek(fp, 0, SEEK_END);
        fclose(fp);

        if (connect(destination_socket, (struct sockaddr*) &destination_addr, sizeof(destination_addr)) == -1) {
            printf("There was an error while connecting to the server..\n");
            exit(0);
        }

        if (send(destination_socket, signals, sizeof(signals), 0) == -1) {
            printf("There was an error while sending data to the server..\n");
            exit(0);
        }
    } else {
        printf("There was an error while opening the file: %s\n", strerror(errno));
        exit(1);
    }
    close(destination_socket);
}

void *send_rms_to_server(void *args){
    rms_values rms = *((rms_values*) args);
    int destination_socket;
    struct sockaddr_in destination_addr;
    char destination_ip[] = "127.0.0.1";
    float message[] = {rms.x, rms.y, rms.z};

    destination_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (destination_socket == -1) {
        printf("There was a problem while creating the destination socket..\n");
        exit(0);
    }

    destination_addr.sin_family = AF_INET;
    destination_addr.sin_port = htons(DESTINATION_PORT);
    destination_addr.sin_addr.s_addr = inet_addr(destination_ip);

    if (connect(destination_socket, (struct sockaddr*) &destination_addr, sizeof(destination_addr)) == -1) {
        printf("There was an error while connecting to the server..\n");
        exit(0);
    }

    if (send(destination_socket, message, sizeof(message), 0) == -1) {
        printf("There was an error while sending data to the server..\n");
        exit(0);
    }

    pthread_exit(0);
}

rms_values root_mean_squared(){
    FILE *fp;
    rms_values rms;
    char row[BUFFER_SIZE], *token;
    int axis = 0, current_row = 0, linecount = 0, c = EOF;

    rms.x = 0;
    rms.y = 0;
    rms.z = 0;

    fp = fopen(filename, "r");
    if (fp != NULL) {
        while (linecount < rms_last_read_line && (c=fgetc(fp)) != EOF) {
            if (c == '\n')
                linecount++;
        }
        while (fgets(row, BUFFER_SIZE, fp) != NULL && current_row < rms_rows_printed) {
            token = strtok(row, ",");

            while(token != NULL){
                float value = atof(token);
                switch (axis) {
                case 0:
                    rms.x += pow(value, 2);
                    break;
                case 1:
                    rms.y += pow(value, 2);
                    break;
                case 2:
                    rms.z += pow(value, 2);
                    break;
                }
                axis = (axis + 1) %  SERIES_LENGTH;
                token = strtok(NULL, ",");
            }
            current_row++;
            rms_last_read_line++;
        }
        fseek(fp, 0, SEEK_END);
        fclose(fp);

        rms.x = sqrt(rms.x / ROWS_BEFORE_RMS);
        rms.y = sqrt(rms.y / ROWS_BEFORE_RMS);
        rms.z = sqrt(rms.z / ROWS_BEFORE_RMS);
        
    } else {
        printf("There was an error while opening the file: %s\n", strerror(errno));
        exit(1);
    }
    return rms;
} 

void write_to_file(char_to_print argument){
    FILE *fp;
    rms_values rms;
    float value = argument.value;
    int axis = argument.axis;

    pthread_mutex_lock(&mutex);
    if(rms_rows_printed >= ROWS_BEFORE_RMS){
        rms = root_mean_squared();
    }
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&rms_mutex);
    if(rms_rows_printed >= ROWS_BEFORE_RMS){
        pthread_t thread;

        rms_rows_printed = 0;
        pthread_create(&thread, NULL, send_rms_to_server, (void*) &rms);
        pthread_join(thread, NULL);
    }
    pthread_mutex_unlock(&rms_mutex);

    pthread_mutex_lock(&mutex);
    if (signals_rows_printed >= ROWS_BEFORE_SENDING) {
        send_signals_to_server();
    }
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutex);
    fp = fopen(filename, "a");
    if (fp != NULL) {
        if (axis == SERIES_LENGTH -1) {
            fprintf(fp, "%f\n", value);
            rms_rows_printed++; 
            signals_rows_printed++;
        } else {
            fprintf(fp, "%f,", value);
        }
        fclose(fp);
    } else {
        printf("There was an error while opening the file: %s\n", strerror(errno));
        exit(1);
    }
    pthread_mutex_unlock(&mutex);
}

void* handle_client(void *args) {
    int client_sock = *((int*)args);
    char client_message[BUFFER_SIZE];
    ssize_t bytes_received;

    memset(client_message, '\0', sizeof(client_message));

    bytes_received = recv(client_sock, client_message, sizeof(client_message), 0);
    if (bytes_received < 0) {
        printf("There was an error while receiving the data.\n");
        pthread_exit(0);
    }

    int offset = 0;
    while (offset < bytes_received) {
        for (int i = 0; i < SERIES_LENGTH; i++) {
            pthread_t write_thread;
            char_to_print argument;
            argument.value = *((float*)(client_message + offset));
            argument.axis = i;
            offset += sizeof(float);
            
            write_to_file(argument);
        }
    }
    close(client_sock);
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    int server_socket;
    struct sockaddr_in server_addr;
    char server_ip[] = "127.0.0.1";

    fclose(fopen(filename, "w"));

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
                close(*client_sock);
                free(client_sock);
            } else {
                printf("There was an error while accepting the client.\n");
            }
        }
    }
    return 0;
}