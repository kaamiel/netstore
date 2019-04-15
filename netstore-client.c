#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "err.h"
#include "common.h"

#define DEFAULT_CLIENT_PORT_NUMBER 6543
#define PATHNAME "./tmp/"
#define FILE_NAMES_LIST_DELIM "|"
#define DIRECTORY_PERMISSIONS 00755
#define FILE_PERMISSIONS 00644
#define SERVICE_SIZE 20

char file_buffer[FILE_BUFFER_SIZE];
char service[SERVICE_SIZE];

void ensure_directory_exists(const char *);

int main(int argc, char *argv[]) {
    int sock, fd, err;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    ssize_t rcv_len, wrote;
    size_t file_number, read_bytes, read_bytes_total, directory_name_length;
    uint16_t response, file_name_length;
    uint32_t file_names_list_length, beginning_address, end_address, number_of_bytes, refusal_reason;
    char *file_names_list, *file_path = 0;
    char **tokens;

    struct list_of_files_request lof_request = {
            .request_type = htons(LIST_OF_FILES_REQUEST)
    };
    struct file_request f_request = {
            .request_type = htons(FILE_REQUEST)
    };

    struct no_interrupt flags = {
            .do_not_interrupt = 0,
            .failed = 0
    };

    if (argc < 2) {
        fatal("usage: %s host [port-number]\n", argv[0]);
    }

    if (argc == 2) {
        sprintf(service, "%d", DEFAULT_CLIENT_PORT_NUMBER);
    } else {
        strncpy(service, argv[2], SERVICE_SIZE - 1);
    }
    directory_name_length = strlen(PATHNAME);

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(argv[1], service, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s (%s:%d)\n", gai_strerror(err), __FILE__, __LINE__);
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s (%s:%d)\n", gai_strerror(err), __FILE__, __LINE__);
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        syserr("socket (%s:%d)\n", __FILE__, __LINE__);
    }

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        syserr("connect (%s:%d)\n", __FILE__, __LINE__);
    }
    freeaddrinfo(addr_result);

    fprintf(stderr, "\n\nConnected to the server [%d].\n", sock);

    fprintf(stderr, "Sent LIST_OF_FILES_REQUEST to the server.\n");
    write_func(sock, &lof_request, sizeof(lof_request), &flags);

    response = read_uint16_t(sock, &flags);
    if (response != LIST_OF_FILES_RESPONSE) {
        fatal("wrong server response (%s:%d)\n", __FILE__, __LINE__);
    }
    fprintf(stderr, "Received LIST_OF_FILES_RESPONSE from the server.\n");

    file_names_list_length = read_uint32_t(sock, &flags);
    file_names_list = calloc(file_names_list_length + 1, sizeof(char));

    rcv_len = read_loop(sock, file_names_list, file_names_list_length, &flags);
    if (rcv_len < file_names_list_length) {
        syserr("read (%s:%d)\n", __FILE__, __LINE__);
    }

    file_names_list[file_names_list_length] = 0;

    // tokenize file names list
    size_t tokens_length = 1, j = 0;
    tokens = malloc(tokens_length * sizeof(char *));

    tokens[0] = strtok(file_names_list, FILE_NAMES_LIST_DELIM);
    while (tokens[j] != 0) {
        ++j;
        if (j == tokens_length) {
            tokens_length *= 2;
            tokens = realloc(tokens, tokens_length * sizeof(char *));
        }
        tokens[j] = strtok(0, FILE_NAMES_LIST_DELIM);
    }
    if (j == 0) {
        printf("The directory is empty.\n");
        free(file_names_list);
        free(tokens);
        close(sock);
        return 1;
    }

    printf("\nFiles in the directory:\n");
    for (size_t i = 0; i < j; ++i) {
        printf("%zu. %s\n", i + 1, tokens[i]);
    }
    printf("\nEnter the file number, the beginning address and the end address of the fragment of the file.\n");

    printf("file number: ");
    scanf("%zu", &file_number);
    if (file_number < 1 || j < file_number) {
        printf("Wrong file number.\n");
        free(file_names_list);
        free(tokens);
        close(sock);
        return 1;
    }
    printf("beginning address: ");
    scanf("%u", &beginning_address);
    printf("end address: ");
    scanf("%u", &end_address);
    if (end_address < beginning_address) {
        printf("Invalid beginning & end address.\n");
        free(file_names_list);
        free(tokens);
        close(sock);
        return 1;
    }

    number_of_bytes = end_address - beginning_address;
    --file_number;
    file_name_length = (uint16_t) strlen(tokens[file_number]);

    f_request.beginning_address = htonl(beginning_address);
    f_request.number_of_bytes = htonl(number_of_bytes);
    f_request.file_name_length = htons(file_name_length);

    fprintf(stderr, "Sent FILE_REQUEST to the server.\n");
    write_func(sock, &f_request, sizeof(f_request), &flags);
    write_func(sock, tokens[file_number], file_name_length, &flags);

    response = read_uint16_t(sock, &flags);
    switch (response) {
        case REFUSAL_RESPONSE:
            refusal_reason = read_uint32_t(sock, &flags);
            switch (refusal_reason) {
                case BAD_FILE_NAME_ERROR:
                    fprintf(stderr, "Received REFUSAL_RESPONSE.BAD_FILE_NAME_ERROR from the server.\n");
                    printf("Bad file name (selected file no longer exists).\n");
                    break;
                case BAD_BEGINNING_ADDRESS_ERROR:
                    fprintf(stderr, "Received REFUSAL_RESPONSE.BAD_BEGINNING_ADDRESS_ERROR from the server.\n");
                    printf("Bad beginning address (greater than the file size − 1).\n");
                    break;
                case NULL_SIZE_ERROR:
                    fprintf(stderr, "Received REFUSAL_RESPONSE.NULL_SIZE_ERROR from the server.\n");
                    printf("Null-size fragment specified.\n");
                    break;
                default:
                    fatal("wrong refusal reason (%s:%d)\n", __FILE__, __LINE__);
            }
            break;
        case FILE_RESPONSE:
            fprintf(stderr, "Received FILE_RESPONSE from the server.\n");
            number_of_bytes = read_uint32_t(sock, &flags);

            ensure_directory_exists(PATHNAME);

            file_path = malloc(directory_name_length + file_name_length + 1);
            strcpy(file_path, PATHNAME);
            strcpy(file_path + directory_name_length, tokens[file_number]);

            fd = open(file_path, O_CREAT | O_WRONLY, FILE_PERMISSIONS);
            if (fd < 0) {
                syserr("open (%s:%d)\n", __FILE__, __LINE__);
            }
            if (lseek(fd, beginning_address, SEEK_SET) < 0) {
                syserr("lseek (%s:%d)\n", __FILE__, __LINE__);
            }

            fprintf(stderr, "Starting downloading %u-bytes fragment from the server.\n", number_of_bytes);
            read_bytes_total = 0;
            read_bytes = 1;
            fprintf(stderr, "Downloading...\n");
            while (read_bytes_total < number_of_bytes && read_bytes > 0) {
                read_bytes = read_loop(sock, file_buffer, MIN(FILE_BUFFER_SIZE, number_of_bytes - read_bytes_total),
                                       &flags);
                wrote = write(fd, file_buffer, read_bytes);
                if (wrote < 0 || (size_t) wrote != read_bytes) {
                    syserr("write; %s:%d\n", __FILE__, __LINE__);
                }
                read_bytes_total += read_bytes;
            }
            fprintf(stderr, "Downloaded %zu-bytes fragment from the server.\n", read_bytes_total);
            if (read_bytes_total < number_of_bytes) {
                syserr("download (%s:%d)\n", __FILE__, __LINE__);
            }

            if (close(fd) == -1) {
                syserr("close (%s:%d)\n", __FILE__, __LINE__);
            }
            break;
        default:
            fatal("wrong server response (%s:%d)\n", __FILE__, __LINE__);
    }

    free(file_names_list);
    free(tokens);
    free(file_path);

    fprintf(stderr, "Ending connection with the server [%d].\n\n", sock);
    if (close(sock) < 0) { // socket would be closed anyway when the program ends
        syserr("close (%s:%d)\n", __FILE__, __LINE__);
    }

    return 0;
}

void ensure_directory_exists(const char *path) {
    struct stat statbuf;

    if (stat(path, &statbuf) != 0) {
        if (mkdir(path, DIRECTORY_PERMISSIONS) != 0) {
            syserr("mkdir (%s:%d)\n", __FILE__, __LINE__);
        }
    } else {
        if (!S_ISDIR(statbuf.st_mode)) {
            syserr("ensure_directory_exists (%s:%d)\n", __FILE__, __LINE__);
        }
    }
}