#include <stdlib.h>
#include <postgresql/libpq-fe.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>


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
void start_http_server(struct User *gebruikers, int rows);

int main(void) {
    const char *database_url = getenv("DATABASE_URL");

    if (!database_url) {
        printf("Zet DATABASE_URL als environment variable.\n");
        return 1;
    }

    PGconn *conn = PQconnectdb(database_url);

    if(PQstatus(conn) != CONNECTION_OK) {
        printf("Connection failed:\n%s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    }

    printf("Connected\n");

    PGresult *result = PQexec(conn, "SELECT * FROM \"User\";");

    if(PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Query failed: \n%s\n", PQerrorMessage(conn));

        PQclear(result);
        PQfinish(conn);

        return 1;
    }

    int rows = PQntuples(result);
    int cols = PQnfields(result);
    
    printf("\nFound %d rows", rows);
    printf("\nFound %d cols\n\n", cols);

    struct User gebruikers[rows];

     // Alle data printen
    for (int row = 0; row < rows; row++) {

        // Veld 0: id (text)
        strncpy(gebruikers[row].id, PQgetvalue(result, row, 0), sizeof(gebruikers[row].id) - 1);
        gebruikers[row].id[sizeof(gebruikers[row].id) - 1] = '\0';

        // Veld 1: name (text)
        strncpy(gebruikers[row].name, PQgetvalue(result, row, 1), sizeof(gebruikers[row].name) - 1);
        gebruikers[row].name[sizeof(gebruikers[row].name) - 1] = '\0';

        // Veld 2: email (text)
        strncpy(gebruikers[row].email, PQgetvalue(result, row, 2), sizeof(gebruikers[row].email) - 1);
        gebruikers[row].email[sizeof(gebruikers[row].email) - 1] = '\0';

        // Veld 4: password (text) - Let op: we slaan index 3 (emailVerified) over!
        strncpy(gebruikers[row].password, PQgetvalue(result, row, 4), sizeof(gebruikers[row].password) - 1);
        gebruikers[row].password[sizeof(gebruikers[row].password) - 1] = '\0';

        // Veld 5: image (text)
        strncpy(gebruikers[row].image, PQgetvalue(result, row, 5), sizeof(gebruikers[row].image) - 1);
        gebruikers[row].image[sizeof(gebruikers[row].image) - 1] = '\0';

        // Veld 6: createdAt (timestamp)
        strncpy(gebruikers[row].createdAt, PQgetvalue(result, row, 6), sizeof(gebruikers[row].createdAt) - 1);
        gebruikers[row].createdAt[sizeof(gebruikers[row].createdAt) - 1] = '\0';

        // Veld 7: updatedAt (timestamp)
        strncpy(gebruikers[row].updatedAt, PQgetvalue(result, row, 7), sizeof(gebruikers[row].updatedAt) - 1);
        gebruikers[row].updatedAt[sizeof(gebruikers[row].updatedAt) - 1] = '\0';

        // Veld 8: stripe_customer_id (text)
        strncpy(gebruikers[row].stripe_customer_id, PQgetvalue(result, row, 8), sizeof(gebruikers[row].stripe_customer_id) - 1);
        gebruikers[row].stripe_customer_id[sizeof(gebruikers[row].stripe_customer_id) - 1] = '\0';
    }

    PQclear(result);
    PQfinish(conn);

    start_http_server(gebruikers, rows);
 
    return 0;
}



#define PORT 8080


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
    printf("%d\n", address.sin_port);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 3. Ga in de 'luister'-modus staan
    listen(server_fd, 1);
    printf("Server draait op http://localhost:%d\n", PORT);

    // 4. De Oneindige Server Loop
    while (1) {
        // Wacht totdat een browser (client) verbinding maakt
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        char *client_ip = inet_ntoa(address.sin_addr);
        int client_poort = ntohs(address.sin_port); // ntohs is de tegenhanger van htons
        
        printf("Bezoeker binnengekomen vanaf IP: %s op poort: %d\n", client_ip, client_poort);

        // Lees het binnenkomende HTTP-verzoek (omdat we C leren, negeren we de inhoud nu even)
        char request_buffer[2048] = {0};
        read(new_socket, request_buffer, sizeof(request_buffer));
        printf("--- BINNENKOMEND BERICHT ---\n%s\n-------------------------\n", request_buffer);

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

    // // 1. De gegevens die we willen invoegen (dit kunnen ook variabelen uit je structs zijn)
    // char nieuwe_id[]    = "uuid-123457";
    // char nieuwe_naam[]  = "Kajrir de C-Master";
    // char nieuwe_email[] = "kajrir2@example.com";

    // // 2. We reserveren een buffer in het RAM voor de totale SQL-query string.
    // // 1024 bytes is ruim voldoende voor deze query.
    // char query_buffer[1024];

    // // 3. We bouwen de query dynamisch op met snprintf
    // // %s staat voor 'string'. Let goed op de \" rondom de tabelnaam User!
    // snprintf(query_buffer, sizeof(query_buffer),
    //          "INSERT INTO \"User\" (id, name, email, \"updatedAt\") "
    //          "VALUES ('%s', '%s', '%s', NOW());",
    //          nieuwe_id, nieuwe_naam, nieuwe_email);

    // // Tip: Print de query even uit om te controleren hoe hij er in puur SQL uitziet!
    // printf("Uit te voeren SQL:\n%s\n\n", query_buffer);

    // // 4. Stuur de query naar PostgreSQL
    // PGresult *insert_result = PQexec(conn, query_buffer);

    // // 5. Controleer of het invoegen is gelukt (bij INSERT verwachten we PGRES_COMMAND_OK)
    // if (PQresultStatus(insert_result) != PGRES_COMMAND_OK) {
    //     printf("INSERT mislukt:\n%s\n", PQerrorMessage(conn));
        
    //     // Foutafhandeling: opruimen en stoppen
    //     PQclear(insert_result);
    //     PQfinish(conn);
    //     return 1;
    // }

    // printf("Nieuwe gebruiker succesvol toegevoegd!\n");

    // // 6. Altijd direct het resultaat van de query opruimen!
    // PQclear(insert_result);
