#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int DEBUG;
#define BUFSIZE 1024

#define CALL(n, msg)                                              \
    if ((n) < 0) {                                                \
        fprintf(stderr, "%s (%s:%d)\n", msg, __FILE__, __LINE__); \
        exit(EXIT_FAILURE);                                       \
    }
#define LOG(...)                          \
    do {                                  \
        if (DEBUG) {                      \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n");        \
        }                                 \
    } while (0);

/* The Sample format to use */
static const pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE, .rate = 40100, .channels = 2};

/* A simple routine calling UNIX write() in a loop */
static ssize_t loop_write(int fd, const void* data, size_t size) {
    ssize_t ret = 0;
    while (size > 0) {
        ssize_t r;
        if ((r = write(fd, data, size)) < 0) return r;
        if (r == 0) break;
        ret += r;
        data = (const char*)data + r;
        size -= (size_t)r;
    }
    return ret;
}

/* A simple routine calling UNIX read() in a loop */
static ssize_t loop_read(int fd, void* data, size_t size) {
    ssize_t ret = 0;
    while (size > 0) {
        ssize_t r;
        if ((r = read(fd, data, size)) < 0) return r;
        if (r == 0) break;
        ret += r;
        data = (char*)data + r;
        size -= (size_t)r;
    }
    return ret;
}

int send_data(int fd, const void* data, size_t size) {
    return loop_write(fd, data, size);
}

int read_data(int fd, void* data, size_t size) {
    return loop_read(fd, data, size);
}

struct Connection {
    int sd;
    char* clientName;
};

// Receiver thread function
void* receive_voice_messages(void* ptr) {
    struct Connection connection = *(struct Connection*)ptr;

    int sd = connection.sd;
    char* clientName = connection.clientName;

    pa_simple* s = NULL;
    int ret = 1;
    int error;

    /* Create a new playback stream */
    if (!(s = pa_simple_new(NULL, clientName, PA_STREAM_PLAYBACK, NULL,
                            "playback", &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__ ": pa_simple_new() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    for (;;) {
        uint8_t buf[BUFSIZE];
        ssize_t r;
        // #if 1
        //         pa_usec_t latency;
        //         if ((latency = pa_simple_get_latency(s, &error)) ==
        //         (pa_usec_t) -1) {
        //             fprintf(stderr, __FILE__":
        //             pa_simple_get_playback_latency() failed: %s\n",
        //             pa_strerror(error)); goto finish;
        //         }
        //         fprintf(stderr, "%0.0f usec    \r", (float)latency);
        // #endif
        /* Read some data ... */
        if ((r = read(sd, buf, sizeof(buf))) <= 0) {
            if (r == 0) /* EOF */
                break;
            fprintf(stderr, __FILE__ ": read() failed: %s\n", strerror(errno));
            goto finish;
        }
        /* ... and play it */
        if (pa_simple_write(s, buf, (size_t)r, &error) < 0) {
            fprintf(stderr, __FILE__ ": pa_simple_write() failed: %s\n",
                    pa_strerror(error));
            goto finish;
        }
    }
    /* Make sure that every single sample was played */
    if (pa_simple_drain(s, &error) < 0) {
        fprintf(stderr, __FILE__ ": pa_simple_drain() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    ret = 0;
finish:
    if (s) pa_simple_free(s);

    return NULL;
}

void* send_voice_messages(void* ptr) {
    struct Connection connection = *(struct Connection*)ptr;

    int sd = connection.sd;
    char* clientName = connection.clientName;

    printf("sd = %d\n", sd);
    printf("clientName = %s\n", clientName);

    pa_simple* s = NULL;
    int ret = 1;
    int error;
    /* Create the recording stream */

    if (!(s = pa_simple_new(NULL, clientName, PA_STREAM_RECORD, NULL, "record",
                            &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__ ": pa_simple_new() failed: %s\n",
                pa_strerror(error));
        goto finish;
    }
    for (;;) {
        uint8_t buf[BUFSIZE];
        /* Record some data ... */
        if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, __FILE__ ": pa_simple_read() failed: %s\n",
                    pa_strerror(error));
            goto finish;
        }
        /* And write it to STDOUT */
        if (loop_write(sd, buf, sizeof(buf)) != sizeof(buf)) {
            fprintf(stderr, __FILE__ ": write() failed: %s\n", strerror(errno));
            goto finish;
        }
    }
    ret = 0;
finish:
    if (s) pa_simple_free(s);
    return NULL;
}