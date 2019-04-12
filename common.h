#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Error handling
#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)

#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)


#define MAX_CONNESSIONI 3
#define DEBUG 1
#define BUFFERSIZE 1000000
#define RECEIVER_SLEEP_S 20 * 1000
#define SENDER_SLEEP_S 300 * 1000
#define PORT 8888
#define WORLD_LOOP_SLEEP 100 * 1000
#define WORLD_SIZE 512
#define TIME_TO_SLEEP 0.1
#define SERVER_ADDRESS "127.0.0.1"
#define RECEIVER_SLEEP_C 50000
#define UNTOUCHED 0
#define TOUCHED 1
#define HIDE_RANGE 5
#endif