#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

int sd;  // socket descriptor
pthread_t receiver_thread;
pthread_t send_thread;

// Function to close resource handles and gracefully shutdown
void killServer();

// Interrupt handler
void handle_my(int sig);

// Server thread function
void *connection_handler(void *nsd);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <server port>", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    sd = socket(AF_INET, SOCK_STREAM, 0);

    // acquiring port
    socklen_t clientLen;

    struct sockaddr_in server, client;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    int trueV = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &trueV, sizeof(int));

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    listen(sd, 1);  // Waiting for connections

    printf("Waiting for the client...\n");

    clientLen = sizeof(client);

    int nsd = accept(sd, (struct sockaddr *)&client, &clientLen);
    struct Connection connection = {
        .sd = nsd,
        .clientName = argv[0],
    };
    printf("Connection Established\n");

    // Start receiver thread to receive call from others
    CALL(pthread_create(&receiver_thread, NULL, receive_voice_messages,
                        (void *)&connection),
         "Receiver thread");

    // Start receiver thread to receive call from others
    // CALL(pthread_create(&send_thread, NULL, send_voice_messages,
    //                     (void *)&connection),
    //      "Send thread");

    pthread_join(receiver_thread, NULL);
    // pthread_join(send_thread, NULL);
}

// Function to close resource handles and gracefully shutdown
void killServer() {
    char response[10];

    printf("Are you sure you want to close the server ? (Y/N) \n");
    scanf("%s", response);

    if (response[0] == 'Y' || response[0] == 'y') {
        pthread_kill(receiver_thread, SIGINT);
        pthread_kill(send_thread, SIGINT);
        close(sd);

        exit(EXIT_SUCCESS);
    }
}

// Interrupt handler
void handle_my(int sig) {
    switch (sig) {
        case SIGINT:
            killServer();
            break;
    }
}
