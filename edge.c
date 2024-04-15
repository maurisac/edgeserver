#include "stdlib.h"
#include "stdio.h"
#include "pthread.h"
#include "unistd.h"
#include "sys/socket.h"
#include "arpa/inet.h"
#include "math.h"     
#include "string.h"                  

typedef struct argomenti{
    float *values;
    char dimension;
} argomenti;

void* rms(void *args){
    FILE *fp;
    argomenti *inp = ((argomenti*)args);
    float *valori = inp->values;
    char dimensione = inp->dimension;


    int somma = 0;
    float res;
    int len = sizeof(valori) / sizeof(valori[0]);

    
    for (int i = 0; i < len; i++){
        somma += pow(valori[i], 2);
    }
    res = sqrt(somma / len);

    char *filename;
    strcat(filename, dimensione);
    strcat(filename, ".csv");

    fp = fopen(filename, "a");

    if (fp != NULL) {
        fprintf(fp, "%f, ", res);
    } else {

    }
    
} 


void* connect(void *args){
    int client_sock = *((int*)args);
    char client_message[20000];

    recv(client_sock, client_message, sizeof(client_message), 0);
}





int main(){

int socket_desc, client_size;
struct sockaddr_in server_addr, client_addr;
int porta = 2000;
char indirizzo[] = "127.0.0.1";
pthread_t thread;

socket_desc = socket(AF_INET, SOCK_DGRAM,0);


//set port and IP
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(porta);
server_addr.sin_addr.s_addr = inet_addr(indirizzo);


//bind to the sert port and IP
bind(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr));

//listen for clients
listen(socket_desc, 3);

while (1){
    client_size = sizeof(client_addr);
    int *client_sock = malloc(sizeof(int));
    *client_sock = accept(socket_desc, (struct sockaddr*)&client_addr,&client_size);
    if (*client_sock != -1){
        pthread_create(&thread,NULL,&connect,(void*) client_sock);
    }
}




}