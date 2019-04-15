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
#include "structs.h"

#define DEFAULT_CLIENT_PORT_NUMBER 6543
#define PATHNAME "./tmp/"
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
    ssize_t rcv_len;
    size_t file_number, read_bytes, read_bytes_total, directory_name_length;
    uint16_t response, file_name_length;
    uint32_t file_names_list_length, beginning_address, end_address, number_of_bytes, refusal_reason;
    char *file_names_list, *file_path;
    char **tokens;
    const char *delim = "|";

    struct list_of_files_request lof_request = {
            .request_type = htons(LIST_OF_FILES_REQUEST)
    };
    struct file_request f_request = {
            .request_type = htons(FILE_REQUEST)
    };

    if (argc < 2) {
        fatal("Usage: %s host [port-number]\n", argv[0]);
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
        syserr("getaddrinfo: %s; %s:%d\n", gai_strerror(err), __FILE__, __LINE__);
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s; %s:%d\n", gai_strerror(err), __FILE__, __LINE__);
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        syserr("socket; %s:%d\n", __FILE__, __LINE__);
    }

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        syserr("connect; %s:%d\n", __FILE__, __LINE__);
    }

    freeaddrinfo(addr_result);

    // list_of_files_request
    printf("\033[1;36m\tWriting to socket: LIST_OF_FILES_REQUEST (%zu bytes).\033[0m\n", sizeof(lof_request));
    write_func(sock, &lof_request, sizeof(lof_request));

    // response
    response = read_uint16_t(sock);
    printf("\033[1;36m\tresponse: %hu\033[0m\n", response);
    if (response != LIST_OF_FILES_RESPONSE) {
        fatal("wrong response; %s:%d\n", __FILE__, __LINE__);
    }

    file_names_list_length = read_uint32_t(sock);
//    printf("\033[1;36m\tfile_names_list_length: %hu\033[0m\n", file_names_list_length);

    file_names_list = calloc(file_names_list_length + 1, sizeof(char));

    rcv_len = read_loop(sock, file_names_list, file_names_list_length);
    printf("\033[1;36m\tread from socket: %zu bytes\033[0m\n", rcv_len);

    file_names_list[file_names_list_length] = 0;
//    printf("\033[1;36m\tfile_names_list: %s\033[0m\n", file_names_list);

    size_t tokens_length = 1, j = 0;
    tokens = malloc(tokens_length * sizeof(char *));

    tokens[0] = strtok(file_names_list, delim);
    while (tokens[j] != 0) {
        ++j;
        if (j == tokens_length) {
            tokens_length *= 2;
            tokens = realloc(tokens, tokens_length * sizeof(char *));
        }
        tokens[j] = strtok(0, delim);
    }

    if (j == 0) {
        close(sock);
        return 0;
    }

    for (size_t i = 0; i < j; ++i) {
        printf("%zu. %s\n", i + 1, tokens[i]);
    }

    printf("Podaj numer pliku, adres początku fragmentu i adres końca fragmentu\n");;
    scanf("%zu\n%u\n%u", &file_number, &beginning_address, &end_address);
    if (file_number > j) {
        fatal("bad file number; %s:%d\n", __FILE__, __LINE__);
    }
    if (end_address < beginning_address) {
        fatal("bad beginning & end address; %s:%d\n", __FILE__, __LINE__);
    }

    number_of_bytes = end_address - beginning_address;
    --file_number;
    file_name_length = (uint16_t) strlen(tokens[file_number]);

    f_request.beginning_address = htonl(beginning_address);
    f_request.number_of_bytes = htonl(number_of_bytes);
    f_request.file_name_length = htons(file_name_length);

    printf("\033[1;36m\tWriting to socket: FILE_REQUEST (%zu bytes).\033[0m\n", sizeof(f_request));
    write_func(sock, &f_request, sizeof(f_request));

    printf("\033[1;36m\tWriting to socket: FILE_REQUEST cd. (%hu bytes).\033[0m\n", file_name_length);
    write_func(sock, tokens[file_number], file_name_length);

    response = read_uint16_t(sock);
    switch (response) {
        case REFUSAL_RESPONSE:
            refusal_reason = read_uint32_t(sock);
            switch (refusal_reason) {
                case BAD_FILE_NAME_ERROR:
                    fatal("bad file name error; %s:%d\n", __FILE__, __LINE__);
                    break;
                case BAD_BEGINNING_ADDRESS_ERROR:
                    fatal("bad beginning address error; %s:%d\n", __FILE__, __LINE__);
                    break;
                case NULL_SIZE_ERROR:
                    fatal("null size error; %s:%d\n", __FILE__, __LINE__);
                    break;
                default:
                    fatal("wrong refusal reason; %s:%d\n", __FILE__, __LINE__);
            }
            break;
        case FILE_RESPONSE:
            number_of_bytes = read_uint32_t(sock);

            ensure_directory_exists(PATHNAME);

            file_path = malloc(directory_name_length + file_name_length + 1);
            strcpy(file_path, PATHNAME);
            strcpy(file_path + directory_name_length, tokens[file_number]);

            fd = open(file_path, O_CREAT | O_WRONLY, FILE_PERMISSIONS);
            if (fd < 0) {
                syserr("open; %s:%d\n", __FILE__, __LINE__);
            }
            lseek(fd, beginning_address, SEEK_SET);

            read_bytes_total = 0;
            read_bytes = 1;
            while (read_bytes_total < number_of_bytes && read_bytes > 0) {
                read_bytes = read_loop(sock, file_buffer, FILE_BUFFER_SIZE);
                write(fd, file_buffer, read_bytes);
                read_bytes_total += read_bytes;
            }

            printf("\033[1;36m\tread from socket: %zu bytes\033[0m\n", read_bytes_total);

            if (close(fd) == -1) {
                syserr("close; %s:%d\n", __FILE__, __LINE__);
            }

            break;
        default:
            fatal("wrong response; %s:%d\n", __FILE__, __LINE__);
    }

    free(file_names_list);
    (void) close(sock); // socket would be closed anyway when the program ends

    return 0;
}

void ensure_directory_exists(const char *path) {
    struct stat statbuf;

    if (stat(path, &statbuf) != 0) {
        if (mkdir(path, DIRECTORY_PERMISSIONS) != 0) {
            syserr("mkdir; %s:%d\n", __FILE__, __LINE__);
        }
    } else {
        if (!S_ISDIR(statbuf.st_mode)) {
            syserr("ensure_directory_exists; %s:%d\n", __FILE__, __LINE__);
        }
    }
}