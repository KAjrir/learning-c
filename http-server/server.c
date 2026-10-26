#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

#define PORT 8080


struct User {
    char id[128];
    char name[128];
    char email[128];
    char password[255];
    char image[255];
    char createdAt[64];
    char updatedAt[64];
    char stripe_customer_id[128];
};

void serialize_users_to_json(struct User *gebruikers, int rows, char *output_buffer, size_t buffer_size);

void start_http_server(struct User *gebruikers, int rows) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 1. Maak een netwerk-socket aan (IPv4, TCP)
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Koppel de socket aan Poort 8080
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Luister op alle netwerkkaarten
    address.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 3. Ga in de 'luister'-modus staan
    listen(server_fd, 3);
    printf("Server draait op http://localhost:%d\n", PORT);

    // 4. De Oneindige Server Loop
    while (1) {
        // Wacht totdat een browser (client) verbinding maakt
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        // Lees het binnenkomende HTTP-verzoek (omdat we C leren, negeren we de inhoud nu even)
        char request_buffer[1024] = {0};
        read(new_socket, request_buffer, 1024); 

        // 5. Bouw de JSON-data met onze serialization-functie
        char json_emmer[4096] = {0};
        serialize_users_to_json(gebruikers, rows, json_emmer, sizeof(json_emmer));

        // 6. Bouw de HTTP-header. Een browser snapt pas JSON als je zegt: Content-Type: application/json
        char http_response[8192] = {0};
        snprintf(http_response, sizeof(http_response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(json_emmer), json_emmer);

        // 7. Stuur de complete HTTP-respons (bytes) terug over de netwerk-socket!
        write(new_socket, http_response, strlen(http_response));
        
        // Sluit de specifieke verbinding met deze client, klaar voor de volgende!
        close(new_socket);
    }
}

// We geven de functie een array van gebruikers, het aantal, 
// en de 'emmer' (output_buffer) waar de JSON in moet worden geschreven.
void serialize_users_to_json(struct User *gebruikers, int rows, char *output_buffer, size_t buffer_size) {
    // We beginnen de JSON array string
    int geschreven = snprintf(output_buffer, buffer_size, "[\n");
    
    for (int i = 0; i < rows; i++) {
        // Voor elk object bouwen we de JSON-structuur op
        // We moeten telkens kijken hoeveel ruimte er nog over is in de emmer (buffer_size - geschreven)
        int n = snprintf(output_buffer + geschreven, buffer_size - geschreven,
            "  {\n"
            "    \"id\": \"%s\",\n"
            "    \"name\": \"%s\",\n"
            "    \"email\": \"%s\"\n"
            "  }%s\n",
            gebruikers[i].id,
            gebruikers[i].name,
            gebruikers[i].email,
            (i < rows - 1) ? "," : "" // Komma na elk object, behalve de laatste
        );
        geschreven += n;
    }
    
    // Sluit de JSON array af
    snprintf(output_buffer + geschreven, buffer_size - geschreven, "]\n");
}
