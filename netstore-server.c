#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>

#include "err.h"
#include "common.h"

#define DEFAULT_SERVER_PORT_NUMBER 6543
#define FILE_NAMES_LIST_BUFFER_SIZE ((65536 * 257) - 1)
#define FILE_NAME_BUFFER_SIZE 256
#define QUEUE_LENGTH SOMAXCONN

#define CHECK_CONDITION(condition) { \
    if ((condition)) {\
        fprintf(stderr, "An error occurred (%s:%d). Ending connection with the client [%d].\n", __FILE__, __LINE__, msg_sock); \
        close(msg_sock); \
        continue; \
    } \
};

#define CHECK_FLAGS(call) { \
    (call); \
    CHECK_CONDITION(flags.failed); \
}

char file_names_list_buffer[FILE_NAMES_LIST_BUFFER_SIZE];
char file_name[FILE_NAME_BUFFER_SIZE];
char file_buffer[FILE_BUFFER_SIZE];

uint32_t get_file_names_list(char *, char, struct no_interrupt *);

int main(int argc, char *argv[]) {
    int sock, msg_sock, fd;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    size_t read_bytes, read_bytes_total;
    ssize_t rcv_len;
    uint16_t request, file_name_length;
    uint32_t file_names_list_length, beginning_address, number_of_bytes;
    in_port_t port_number;
    struct stat statbuf;

    struct list_of_files_response lof_response = {
            .response_type = htons(LIST_OF_FILES_RESPONSE)
    };
    struct refusal_response r_response = {
            .response_type = htons(REFUSAL_RESPONSE)
    };
    struct file_response f_response = {
            .response_type = htons(FILE_RESPONSE)
    };

    struct no_interrupt flags = {
            .do_not_interrupt = 1
    };

    if (argc < 2) {
        fatal("Usage: %s directory-name [port-number]\n", argv[0]);
    }

    if (argc == 2) {
        port_number = DEFAULT_SERVER_PORT_NUMBER;
    } else {
        port_number = (in_port_t) strtol(argv[2], 0, 0);
    }

    if (chdir(argv[1]) < 0) {
        syserr("chdir (%s:%d)\n", __FILE__, __LINE__);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0) {
        syserr("socket (%s:%d)\n", __FILE__, __LINE__);
    }
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port_number); // listening on port port_number

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        syserr("bind (%s:%d)\n", __FILE__, __LINE__);
    }

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0) {
        syserr("listen (%s:%d)\n", __FILE__, __LINE__);
    }

    printf("Accepting client connections on port %hu.\n", ntohs(server_address.sin_port));
    for (;;) {
        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0) {
            syserr("accept (%s:%d)\n", __FILE__, __LINE__);
        }

        fprintf(stderr, "\n\nConnected to a new client [%d].\n", msg_sock);
        flags.failed = 0;

        CHECK_FLAGS(request = read_uint16_t(msg_sock, &flags))
        if (request == LIST_OF_FILES_REQUEST) {
            fprintf(stderr, "Received LIST_OF_FILES_REQUEST from the client.\n");

            CHECK_FLAGS(file_names_list_length = get_file_names_list(file_names_list_buffer, '|', &flags));
            lof_response.file_names_list_length = htonl(file_names_list_length);

            fprintf(stderr, "Sent LIST_OF_FILES_RESPONSE to the client.\n");
            CHECK_FLAGS(write_func(msg_sock, &lof_response, sizeof(lof_response), &flags))
            CHECK_FLAGS(write_func(msg_sock, file_names_list_buffer, file_names_list_length, &flags))

            CHECK_FLAGS(request = read_uint16_t(msg_sock, &flags))
        }

        if (request == FILE_REQUEST) {
            fprintf(stderr, "Received FILE_REQUEST from the client.\n");
            CHECK_FLAGS(beginning_address = read_uint32_t(msg_sock, &flags))
            CHECK_FLAGS(number_of_bytes = read_uint32_t(msg_sock, &flags))
            CHECK_FLAGS(file_name_length = read_uint16_t(msg_sock, &flags))
            CHECK_CONDITION(255 < file_name_length)

            CHECK_FLAGS(rcv_len = read_loop(msg_sock, file_name, file_name_length, &flags))
            CHECK_CONDITION(rcv_len < file_name_length)

            file_name[file_name_length] = 0;

            CHECK_CONDITION(strchr(file_name, '/') != 0)

            if (stat(file_name, &statbuf) != 0) {
                r_response.refusal_reason = htonl(BAD_FILE_NAME_ERROR);

                fprintf(stderr, "Sent REFUSAL_RESPONSE.BAD_FILE_NAME_ERROR to the client.\n");
                CHECK_FLAGS(write_func(msg_sock, &r_response, sizeof(r_response), &flags))
            } else if (statbuf.st_size - 1 < beginning_address) {
                r_response.refusal_reason = htonl(BAD_BEGINNING_ADDRESS_ERROR);

                fprintf(stderr, "Sent REFUSAL_RESPONSE.BAD_FILE_NAME_ERROR to the client.\n");
                CHECK_FLAGS(write_func(msg_sock, &r_response, sizeof(r_response), &flags))
            } else if (number_of_bytes == 0) {
                r_response.refusal_reason = htonl(NULL_SIZE_ERROR);

                fprintf(stderr, "Sent REFUSAL_RESPONSE.BAD_FILE_NAME_ERROR to the client.\n");
                CHECK_FLAGS(write_func(msg_sock, &r_response, sizeof(r_response), &flags))
            } else {
                number_of_bytes = (uint32_t) MIN(number_of_bytes, statbuf.st_size - beginning_address);
                f_response.number_of_bytes = htonl(number_of_bytes);

                fprintf(stderr, "Sent FILE_RESPONSE to the client.\n");
                CHECK_FLAGS(write_func(msg_sock, &f_response, sizeof(f_response), &flags))

                fd = open(file_name, O_RDONLY);
                CHECK_CONDITION(fd < 0)
                CHECK_CONDITION(lseek(fd, beginning_address, SEEK_SET) < 0);

                fprintf(stderr, "Starting uploading %u-bytes fragment to the client.\n", number_of_bytes);
                read_bytes_total = 0;
                read_bytes = 1;
                fprintf(stderr, "Uploading...\n");
                while (read_bytes_total < number_of_bytes && read_bytes > 0) {
                    CHECK_FLAGS(read_bytes = read_loop(fd, file_buffer,
                                                       MIN(FILE_BUFFER_SIZE, number_of_bytes - read_bytes_total),
                                                       &flags))
                    CHECK_FLAGS(write_func(msg_sock, file_buffer, read_bytes, &flags))
                    read_bytes_total += read_bytes;
                }
                fprintf(stderr, "Uploaded %zu-bytes fragment to the client.\n", read_bytes_total);
                CHECK_CONDITION(read_bytes_total < number_of_bytes)

                CHECK_CONDITION(close(fd) == -1)
            }
        } else {
            fprintf(stderr, "Wrong client request.\n");
        }

        fprintf(stderr, "Ending connection with the client [%d].\n", msg_sock);
        CHECK_CONDITION(close(msg_sock) < 0)
    }

    return 0;
}

uint32_t get_file_names_list(char *buffer, char delim, struct no_interrupt *flags) {
    DIR *dirp = opendir("./");
    struct dirent *ent;
    struct stat statbuf;

    if (dirp == 0) {
        if (flags->do_not_interrupt) {
            flags->failed = 1;
        } else {
            syserr("opendir; %s:%d\n", __FILE__, __LINE__);
        }
    }
    uint32_t count = 0;
    while ((ent = readdir(dirp)) != 0) {
        if (stat(ent->d_name, &statbuf) != 0) {
            if (flags->do_not_interrupt) {
                flags->failed = 1;
            } else {
                syserr("stat (%s:%d)\n", __FILE__, __LINE__);
            }
        } else {
            if (S_ISREG(statbuf.st_mode)) {
                strcpy(buffer + count, ent->d_name);
                count += strlen(ent->d_name);
                buffer[count++] = delim;
            }
        }
    }
    if (count > 0) {
        buffer[--count] = 0;
    }
    closedir(dirp);
    return count;
}
