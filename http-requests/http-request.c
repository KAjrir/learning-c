#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

int main(void) {
    const char *host = "localhost";
    const char *path = "/";
    const char *method = "POST";
    const char *port = "3000";

    struct addrinfo hints = {0};
    struct addrinfo *res;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    struct addrinfo *p = res;
    while (p != NULL) {
        printf("\n--- addrinfo ---\n");
        printf("flags:    %d\n", p->ai_flags);
        printf("family:   %d\n", p->ai_family);
        printf("socktype: %d\n", p->ai_socktype);
        printf("protocol: %d\n", p->ai_protocol);
        printf("addrlen:  %u\n", (unsigned)p->ai_addrlen);
        printf("addr:     %p\n", (void *)p->ai_addr);
        printf("next:     %p\n", (void *)p->ai_next);

        p = p->ai_next;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (sock < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    FILE *fd = fopen("file.jpeg", "rb");

    if (fd == NULL) {
        perror("fopen");
        return 1;
    }

    unsigned char foto[100000];
    
    size_t foto_size = fread(foto, 1, sizeof(foto), fd);

    char header[256];
    
    int len = snprintf(header, sizeof(header),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        method,
        path,
        host,
        foto_size
    );

    send(sock, header, len, 0);
    send(sock, foto, foto_size, 0);

    char buffer[4096];
    ssize_t n;

    while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
        printf("\n");
    }

    fclose(fd);
    return 0;
}