#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "err.h"

#define FILE_BUFFER_SIZE (512 * 1024)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))


// client requests

#define LIST_OF_FILES_REQUEST 1
#define FILE_REQUEST 2

struct __attribute__((__packed__)) list_of_files_request {
    uint16_t request_type;
};

struct __attribute__((__packed__)) file_request {
    uint16_t request_type;

    uint32_t beginning_address;
    uint32_t number_of_bytes;
    uint16_t file_name_length;
};


// server responses

#define LIST_OF_FILES_RESPONSE 1
#define REFUSAL_RESPONSE 2
#define FILE_RESPONSE 3

#define BAD_FILE_NAME_ERROR 1
#define BAD_BEGINNING_ADDRESS_ERROR 2
#define NULL_SIZE_ERROR 3

struct __attribute__((__packed__)) list_of_files_response {
    uint16_t response_type;

    uint32_t file_names_list_length;
};

struct __attribute__((__packed__)) refusal_response {
    uint16_t response_type;

    uint32_t refusal_reason;
};

struct __attribute__((__packed__)) file_response {
    uint16_t response_type;

    uint32_t number_of_bytes;
};

// helper functions

struct no_interrupt {
    char do_not_interrupt;
    char failed;
};

extern size_t read_loop(int fd, char *buf, size_t count, struct no_interrupt *flags) {
    ssize_t received = 1;
    size_t received_total = 0;
    while (received_total < count && received > 0) {
        received = read(fd, buf + received_total, count - received_total);
        if (received < 0) {
            if (flags->do_not_interrupt) {
                flags->failed = 1;
            } else {
                syserr("read; %s:%d\n", __FILE__, __LINE__);
            }
        }
        received_total += received;
    }
    return received_total;
}

static void read_uint(int fd, void *result, size_t size, struct no_interrupt *flags) {
    char buffer[size];

    if (read_loop(fd, buffer, size, flags) < size) {
        if (flags->do_not_interrupt) {
            flags->failed = 1;
        } else {
            syserr("read_uint; %s:%d\n", __FILE__, __LINE__);
        }
    }
    memcpy(result, buffer, size);
}

static uint16_t read_uint16_t(int fd, struct no_interrupt *flags) {
    uint16_t result;
    read_uint(fd, &result, sizeof(result), flags);
    return ntohs(result);
}

static uint32_t read_uint32_t(int fd, struct no_interrupt *flags) {
    uint32_t result;
    read_uint(fd, &result, sizeof(result), flags);
    return ntohl(result);
}

static void write_func(int fd, const void *buf, size_t count, struct no_interrupt *flags) {
    ssize_t wrote = write(fd, buf, count);
    if (wrote < 0 || (size_t) wrote != count) {
        if (flags->do_not_interrupt) {
            flags->failed = 1;
        } else {
            syserr("write; %s:%d\n", __FILE__, __LINE__);
        }
    }
}

#endif // STRUCTS_H
