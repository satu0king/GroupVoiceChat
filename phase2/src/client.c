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
#include "messages.h"
#include "ntp.h"

struct ChatMessage message;

enum Mode mode = PROD;  // [DEV | TEST | PROD]
int logfileFD;          // log file descriptor
int sd;                 // socket descriptor
pthread_t receiver_thread;
pthread_t send_thread;

// Function to close resource handles and gracefully shutdown
void killClient();

// Interrupt handler
void handle_my(int sig);

// Server thread function
void *connection_handler(void *nsd);

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf(
            "Usage: %s <server IP> <server port> <user handle> <group handle> "
            "[mode [logfile]]\n",
            argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc >= 6) {
        if (!strcmp(argv[5], "TEST")) {
            mode = TEST;

            // Open log file in TEST mode
            logfileFD = open(argv[6], O_WRONLY | O_APPEND | O_CREAT, 0744);
            if (logfileFD < 0) {
                perror("open\n");
                exit(EXIT_FAILURE);
            }
        } else if (!strcmp(argv[5], "DEV")) {
            mode = DEV;
        } else if (!strcmp(argv[5], "PROD")) {
            mode = PROD;
        }
    }

    // Extracting command line information
    int IP = inet_addr(argv[1]);  // INADDR_ANY;
    char *name = argv[3];
    char *groupName = argv[4];

    // NTP Disabled for now as clocks are synchronized
    // if (mode == DEV) runNTP();

    // Setting up signal handlers
    signal(SIGINT, handle_my);

    // Requesting connection with server
    struct sockaddr_in server, client;
    sd = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = IP;
    server.sin_port = htons(atoi(argv[2]));

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect()");
        exit(EXIT_FAILURE);
    }

    // Send a join request with group name and user name
    struct JoinRequest request;
    strcpy(request.name, name);
    strcpy(request.groupName, groupName);

    send_data(sd, &request, sizeof(request));
    struct JoinResponse response;
    read_data(sd, &response, sizeof(response));

    // Read group id and user id
    int connectionId = response.id;
    int groupId = response.groupId;

    if (mode == DEV) printf("Id assigned by server: %d\n", connectionId);

    struct Connection connection = {.sd = sd, .clientName = argv[0]};

    // Start receiver thread to receive call from others
    CALL(pthread_create(&receiver_thread, NULL, receive_voice_messages,
                        (void *)&connection),
         "Receiver thread");

    // Start receiver thread to receive call from others
    CALL(pthread_create(&send_thread, NULL, send_voice_messages,
                        (void *)&connection),
         "Send thread");

    pthread_join(send_thread, NULL);
    pthread_join(receiver_thread, NULL);
}

// Function to close resource handles and gracefully shutdown
void killClient() {
    char response[10];

    if (mode != TEST) {
        printf("Are you sure you want to close the client ? (Y/N) \n");
        scanf("%s", response);
    } else {
        response[0] = 'y';
    }

    if (response[0] == 'Y' || response[0] == 'y') {
        pthread_kill(receiver_thread, SIGINT);
        close(sd);
        if (mode == TEST) {
            close(logfileFD);
        }
        exit(EXIT_SUCCESS);
    }
}

// Interrupt handler
void handle_my(int sig) {
    switch (sig) {
        case SIGINT:
            killClient();
            break;
        case SIGUSR1:
            if (mode != TEST) printf("I received a send message request\n");
            strcpy(message.message, "This is a message sent with ♥\n");
            message.time = getTime();
            send_data(sd, &message, sizeof(message));
            break;
    }
}

// Server thread function
void *connection_handler(void *nsd) {
    int nsfd = *(int *)nsd;

    struct ChatMessage message;

    // Keep receiveing messages from server and print messages from users
    // Calculate delay by using timestamp from packet
    while (read(nsfd, &message, sizeof(message))) {
        if (mode != TEST) printf("%s: %s", message.name, message.message);
        struct timeval receiveTime = getTime();
        struct timeval sendTime = message.time;

        // calculate delay in microseconds
        unsigned long delay = (receiveTime.tv_sec - sendTime.tv_sec) * 1000000 +
                              receiveTime.tv_usec - sendTime.tv_usec;

        if (mode == DEV) {
            printf("Delay: %ld\n", delay);
        } else if (mode == TEST) {
            write(logfileFD, &delay, sizeof(delay));
        }

        fflush(stdout);  // force print to console
    }

    if (mode != TEST) printf("Disconnected from server\n");

    exit(0);
    return NULL;
}
