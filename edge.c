#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>  
#include <string.h>
#include<stdbool.h>

#define ACCEPTED_CLIENTS 3
#define N_VALUES_IN_RMS 50
#define SERVER_PORT 7072
#define DESTINATION_PORT 7001
#define SERIES_LENGTH 3
#define BUFFER_SIZE 5000

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rows_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int rows_printed = 0;
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

void* send_to_server(void *args){
    // Devo mandare sia i valori dei sensori, sia l'RMS
    int destination_socket;
    struct sockaddr_in destination_addr;
    char destination_ip[] = "127.0.0.1";

    destination_socket = socket(AF_INET, SOCK_STREAM, 0);

    destination_addr.sin_family = AF_INET;
    destination_addr.sin_port = htons(SERVER_PORT);
    destination_addr.sin_addr.s_addr = inet_addr(destination_ip);

    bind(destination_socket, (struct sockaddr*) &destination_addr, sizeof(destination_addr));

}

void* root_mean_squared(){
    FILE *fp;
    rms_values rms;
    char row[1024], *token;
    int axis = 0, current_row = 0;

    rms.x = 0;
    rms.y = 0;
    rms.z = 0;
    printf("Getting ready to calculate RMS..\n");
    pthread_mutex_lock(&mutex);
    fp = fopen(filename, "r");
    if (fp != NULL) {
        fseek(fp, 0, SEEK_SET);
        while (fgets(row, 5000, fp) != NULL && current_row < rows_printed) {
            printf("Calculating RMS..\n");
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
        }
        rows_printed = 0;
        fseek(fp, 0, SEEK_END);
        fclose(fp);
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);

        rms.x = sqrt(rms.x / N_VALUES_IN_RMS);
        rms.y = sqrt(rms.y / N_VALUES_IN_RMS);
        rms.z = sqrt(rms.z / N_VALUES_IN_RMS);
        printf("X: %f, Y: %f, Z: %f\n", rms.x, rms.y, rms.z);
    } else {
        printf("There was an error opening the file.\n");
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(0);
} 

void *write_to_file(void *args){
    FILE *fp;
    char_to_print argument = *((char_to_print*)args);
    float value = argument.value;
    int axis = argument.axis;
    printf("Values received: %f - %d.\n", value, axis);

    pthread_mutex_lock(&rows_mutex);
    if(rows_printed >= N_VALUES_IN_RMS){
        root_mean_squared();
        while (rows_printed >= N_VALUES_IN_RMS) {
            pthread_cond_wait(&cond, &mutex);
        }
    }
    pthread_mutex_unlock(&rows_mutex);

    pthread_mutex_lock(&mutex);
    fp = fopen(filename, "a");
    if (fp != NULL) {
        if (axis == SERIES_LENGTH -1) {
            fprintf(fp, "%f\n", value);
            rows_printed++; 
        } else {
            fprintf(fp, "%f,", value);
        }
        fclose(fp);
    } else {
        printf("There was an error opening the file.\n");
        
    }
    pthread_mutex_unlock(&mutex);
    pthread_exit(0);
}

void* handle_client(void *args) {
    int client_sock = *((int*)args);
    char client_message[BUFFER_SIZE];
    FILE *fp;

    ssize_t bytes_received;
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
            
            pthread_create(&write_thread, NULL, write_to_file, (void*) &argument);
            pthread_join(write_thread, NULL);
        }
    }
    close(client_sock);
    pthread_exit(0);
}

int main(int argc, char* argv[]) {
    // Sicuramente serve un'altra socket per inviare i dati al server di storage
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    char server_ip[] = "127.0.0.1";

    // Bisogna vedere se si può usare una socket "normale", oppure se si vede obbligatoriamente usare socket_raw
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
            pthread_t client_thread, write_thread;
            int *client_sock = (int*)malloc(sizeof(int));
            int client_size = sizeof(client_addr);

            *client_sock = accept(server_socket, (struct sockaddr*) &client_addr, &client_size);
            // Mi serve la garanzia di prendere una X, una Y ed una Z per poterle stampare su file CSV sulla stessa linea
            if (*client_sock != -1){

                pthread_create(&client_thread, NULL, handle_client, (void*) client_sock);
                //pthread_create(&write_thread, NULL, write_to_file, NULL);

                pthread_join(client_thread, NULL);
                //pthread_join(write_thread, NULL);
                free(client_sock);
            } else {
                printf("There was an error while accepting the client.\n");
            }
        }
    }
}