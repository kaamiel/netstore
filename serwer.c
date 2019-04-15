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
#include "structs.h"

#define DEFAULT_SERVER_PORT_NUMBER 6543
#define FILE_NAMES_LIST_BUFFER_SIZE ((65536 * 257) - 1)
#define QUEUE_LENGTH     5

#define MIN(a, b) (((a)<(b)) ? (a) : (b))

char file_names_list_buffer[FILE_NAMES_LIST_BUFFER_SIZE];
char file_buffer[FILE_BUFFER_SIZE];

uint32_t get_file_names_list(const char *, char *, char);

int main(int argc, char *argv[]) {
    int sock, msg_sock, fd;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    size_t read_bytes, read_bytes_total, directory_name_length, file_path_length;
    ssize_t rcv_len;
    uint16_t request, file_name_length;
    uint32_t file_names_list_length, beginning_address, number_of_bytes;
    char *file_path;
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

    if (argc < 2) {
        fatal("Usage: %s directory-name [port-number]\n", argv[0]);
    }

    if (argc == 2) {
        port_number = DEFAULT_SERVER_PORT_NUMBER;
    } else {
        port_number = (in_port_t) strtol(argv[2], 0, 0);
    }
    directory_name_length = strlen(argv[1]);

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0) {
        syserr("socket; %s:%d\n", __FILE__, __LINE__);
    }
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port_number); // listening on port port_number

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        syserr("bind; %s:%d\n", __FILE__, __LINE__);
    }

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0) {
        syserr("listen; %s:%d\n", __FILE__, __LINE__);
    }

    printf("\033[1;36m\taccepting client connections on port %hu\n", ntohs(server_address.sin_port));
    for (;;) {
        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0) {
            syserr("accept; %s:%d\n", __FILE__, __LINE__);
        }

        request = read_uint16_t(msg_sock);
        printf("\033[1;36m\trequest: %hu\n", request);
        if (request == LIST_OF_FILES_REQUEST) {
            file_names_list_length = get_file_names_list(argv[1], file_names_list_buffer, '|');

//            printf("\033[1;36m\tfile_names_list_length: %hu\n", file_names_list_length);

            lof_response.file_names_list_length = htonl(file_names_list_length);

            printf("\033[1;36m\tWriting to socket: LIST_OF_FILES_RESPONSE (%zu bytes).\n",
                   sizeof(lof_response));
            write_func(msg_sock, &lof_response, sizeof(lof_response));

            printf("\033[1;36m\tWriting to socket: LIST_OF_FILES_RESPONSE cd. (%hu bytes).\n",
                   file_names_list_length);
            write_func(msg_sock, file_names_list_buffer, file_names_list_length);

            request = read_uint16_t(msg_sock);
            printf("\033[1;36m\trequest: %hu\n", request);
        }

        if (request == FILE_REQUEST) {
            beginning_address = read_uint32_t(msg_sock);
            number_of_bytes = read_uint32_t(msg_sock);
            file_name_length = read_uint16_t(msg_sock);

            printf("\033[1;36m\tbeginning_address: %u, number_of_bytes: %u, file_name_length: %u\n",
                   beginning_address, number_of_bytes, file_name_length);

            file_path_length = directory_name_length + file_name_length + 2;
            file_path = calloc(file_path_length, sizeof(char));

            strcpy(file_path, argv[1]);
            file_path[directory_name_length] = '/';
            rcv_len = read_loop(msg_sock, file_path + directory_name_length + 1, file_name_length);
            printf("\033[1;36m\tread from socket: %zu bytes\033[0m\n", rcv_len);

            file_path[file_path_length - 1] = 0;
            printf("\033[1;36m\tfile_name: %s\033[0m\n", file_path);

            if (stat(file_path, &statbuf) != 0) {
                r_response.refusal_reason = htonl(BAD_FILE_NAME_ERROR);

                printf("\033[1;36m\tWriting to socket: REFUSAL_RESPONSE (%zu bytes).\n", sizeof(r_response));
                write_func(msg_sock, &r_response, sizeof(r_response));
            } else if (statbuf.st_size - 1 < beginning_address) {
                r_response.refusal_reason = htonl(BAD_BEGINNING_ADDRESS_ERROR);

                printf("\033[1;36m\tWriting to socket: REFUSAL_RESPONSE (%zu bytes).\n", sizeof(r_response));
                write_func(msg_sock, &r_response, sizeof(r_response));
            } else if (number_of_bytes == 0) {
                r_response.refusal_reason = htonl(NULL_SIZE_ERROR);

                printf("\033[1;36m\tWriting to socket: REFUSAL_RESPONSE (%zu bytes).\n", sizeof(r_response));
                write_func(msg_sock, &r_response, sizeof(r_response));
            } else {
                f_response.number_of_bytes = htonl(number_of_bytes);

                printf("\033[1;36m\tWriting to socket: FILE_RESPONSE (%zu bytes).\n", sizeof(f_response));
                write_func(msg_sock, &f_response, sizeof(f_response));

                fd = open(file_path, O_RDONLY);
                if (fd < 0) {
                    syserr("open; %s:%d\n", __FILE__, __LINE__);
                }
                lseek(fd, beginning_address, SEEK_SET);

                printf("\033[1;36m\tWriting to socket: FILE (%u bytes).\n", number_of_bytes);

                read_bytes_total = 0;
                read_bytes = 1;
                while (read_bytes_total < number_of_bytes && read_bytes > 0) {
                    read_bytes = read_loop(fd, file_buffer, MIN(FILE_BUFFER_SIZE, number_of_bytes - read_bytes_total));
                    write_func(msg_sock, file_buffer, read_bytes);
                    read_bytes_total += read_bytes;
                }

                if (close(fd) == -1) {
                    syserr("close; %s:%d\n", __FILE__, __LINE__);
                }
            }
        } else {
            fatal("wrong request; %s:%d\n", __FILE__, __LINE__);
        }

        printf("\033[1;36m\tending connection\n");
        if (close(msg_sock) < 0) {
            syserr("close; %s:%d\n", __FILE__, __LINE__);
        }
    }

    return 0;
}

uint32_t get_file_names_list(const char *dir_name, char *buffer, char delim) {
    DIR *dirp = opendir(dir_name);
    struct dirent *ent;

    if (dirp == 0) {
        syserr("opendir; %s:%d\n", __FILE__, __LINE__);
    }
    uint32_t count = 0;
    while ((ent = readdir(dirp)) != 0) {
        if (ent->d_type == DT_REG) {
            strcpy(buffer + count, ent->d_name);
            count += strlen(ent->d_name);
            buffer[count++] = delim;
        }
    }
    buffer[--count] = 0;
    closedir(dirp);
    return count;
}
