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
#include "queue.h"

#define MAX_CLIENTS 1000
#define MAX_GROUPS 10
#define MAX_GROUP_SIZE 200
#define MESSAGE_QUEUE_SIZE 10000
#define CONSUMER_THREAD_COUNT 3

// resource handlers
int connections[MAX_GROUPS][MAX_GROUP_SIZE];
char groupNames[MAX_GROUPS][20];
int connectionCount[MAX_GROUPS];
int groupCount = 0;
int sd;
int logfileFD;

// Producer - Consumer queue
struct Queue *messageQueue;
pthread_t consumer_threads[CONSUMER_THREAD_COUNT];
pthread_t producer_threads[MAX_CLIENTS];
int clientCount = 0;

enum Mode mode = PROD;

// Mutexes
pthread_mutex_t newConnectionLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t messageLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_cons = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_prod = PTHREAD_COND_INITIALIZER;

// Function to close resource handles and gracefully shutdown
void killServer();

// Interrupt handler
void handle_my(int sig);

// Client receiver thread function (producer thread)
void *connection_handler(void *_connection);

// Client deliver thread function (consumer thread)
void *messageDeliver(void *v);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port> [mode [logfile]]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    if (argc >= 3) {
        if (!strcmp(argv[2], "TEST")) {
            mode = TEST;
            logfileFD = open(argv[3], O_WRONLY | O_APPEND | O_CREAT, 0744);
            if (logfileFD < 0) {
                perror("open\n");
                exit(EXIT_FAILURE);
            }

        } else if (!strcmp(argv[2], "DEV")) {
            mode = DEV;
        } else if (!strcmp(argv[2], "PROD")) {
            mode = PROD;
        }
    }

    // Setting up signal handlers
    signal(SIGINT, handle_my);

    // Setup
    messageQueue = init_queue(MESSAGE_QUEUE_SIZE);

    // acquiring port
    socklen_t clientLen;

    struct sockaddr_in server, client;

    sd = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    int trueV = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &trueV, sizeof(int));

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Creating consumer threads
    for (int i = 0; i < CONSUMER_THREAD_COUNT; i++) {
        if (pthread_create(&consumer_threads[i], NULL, messageDeliver, NULL) <
            0) {
            perror("pthread_create()");
            exit(0);
        }
    }

    listen(sd, 10);  // Waiting for connections

    if (mode == DEV) printf("Waiting for the client...\n");

    clientLen = sizeof(client);

    while (1) {
        int *nsd = malloc(sizeof(int));
        *nsd = accept(sd, (struct sockaddr *)&client, &clientLen);
        if (pthread_create(&producer_threads[clientCount++], NULL,
                           connection_handler, (void *)nsd) < 0) {
            perror("pthread_create()");
            exit(0);
        }
        if (mode == DEV) printf("Connection Created\n");
    }

    pthread_exit(NULL);
    close(sd);
}

// Function to close resource handles and gracefully shutdown
void killServer() {
    printf(
        "Are you sure you want to close the server ? All groups will be "
        "deleted (Y/N) \n");
    char response[10];
    scanf("%s", response);
    if (response[0] == 'Y' || response[0] == 'y') {
        printf("Closing Server\n");

        for (int i = 0; i < CONSUMER_THREAD_COUNT; i++) {
            pthread_kill(consumer_threads[i], SIGKILL);
        }
        for (int i = 0; i < clientCount - 1; i++) {
            pthread_kill(producer_threads[i], SIGKILL);
        }

        for (int i = 0; i < groupCount; i++) {
            for (int j = 0; j < connectionCount[i]; j++) {
                close(connections[i][j]);
            }
        }

        close(sd);
        delete_queue(messageQueue);
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

// Client thread function
// The server adds the client to the group requested and assigns a user id. If
// the group does not exist then a new group is created and assigned.
// Appropriate mutexes are used
void *connection_handler(void *_connection) {
    int nsfd = *(int *)_connection;

    // Read join request
    struct JoinRequest request;
    read_data(nsfd, &request, sizeof(request));

    char *name = request.name;
    char *groupName = request.groupName;

    int flag = 1;
    int groupId;
    int id;

    pthread_mutex_lock(&newConnectionLock);
    for (int i = 0; i < groupCount; i++) {
        if (strcmp(groupName, groupNames[i]) == 0) {
            // Group found, assign group
            groupId = i;
            id = connectionCount[i]++;
            connections[groupId][id] = nsfd;
            flag = 0;
            break;
        }
    }

    // Group not found, create and assign new group
    if (flag) {
        int i = groupCount++;
        strcpy(groupNames[i], request.groupName);
        groupId = i;
        id = connectionCount[i]++;
        connections[groupId][id] = nsfd;
    }
    pthread_mutex_unlock(&newConnectionLock);

    // Reply to client about the new connection
    struct JoinResponse response;
    response.id = id;
    response.groupId = groupId;
    send_data(nsfd, &response, sizeof(response));

    if (mode == DEV)
        printf(
            "Client %s joined the chat, ID assigned: %d, Group Id Assigned: "
            "%d\n",
            name, response.id, response.groupId);

    // Wait for messages from client and insert into queue
    struct VoiceMessage message;
    message.groupId = groupId;
    message.id = id;

    while (read_data(nsfd, &message.message, sizeof(message.message))) {
        pthread_mutex_lock(&messageLock);

        while (queue_is_full(messageQueue))
            pthread_cond_wait(&c_prod, &messageLock);

        struct VoiceMessage *messageInsert =
            (struct VoiceMessage *)malloc(sizeof(struct VoiceMessage));
        *messageInsert = message;
        insert_queue(messageQueue, messageInsert);
        pthread_mutex_unlock(&messageLock);
        pthread_cond_signal(&c_cons);
    }

    if (mode == DEV)
        printf("%s with connection ID %d has left the chat\n", name, id);

    connections[groupId][id] = -1;
    free(_connection);
    return NULL;
}

// Client deliver thread function (consumer thread)
void *messageDeliver(void *v) {
    while (1) {
        pthread_mutex_lock(&messageLock);

        while (queue_is_empty(messageQueue))
            pthread_cond_wait(&c_cons,
                              &messageLock);  // Wait for new message to deliver

        struct VoiceMessage *messageSend = pop_queue(messageQueue);

        pthread_mutex_unlock(&messageLock);
        pthread_cond_signal(&c_prod);

        int groupId = messageSend->groupId;
        for (int i = 0; i < connectionCount[groupId]; i++) {
            if (~connections[groupId][i] && i != messageSend->id) {
                send_data(connections[groupId][i], messageSend->message,
                          sizeof(messageSend->message));
            }
        }

        free(messageSend);
    }
    return NULL;
}