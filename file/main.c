#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void){
    FILE *file = fopen("username.csv", "r");

    if (file == NULL) {
        perror("Kon bestand niet openen!");
        return 1;
    }
    
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    printf("File size: %zu\n\n", file_size);
    
    char buffer[file_size + 1];

    char *headers[4];
    int headers_count = 0;


    size_t bytes_read = fread(buffer, 1, file_size, file);
    
    buffer[bytes_read] = '\0';
    
    headers[headers_count++] = buffer;
    
    char *p;
    for(p = buffer; *p != '\0'; p++) {
        if (*p == '\n') {
            *p = '\0'; 
            break;
        }
        if (*p == ';') {
            *p = '\0';
            headers[headers_count++] = p + 1;
        }
    }
    printf("\n");

    printf("%s\n%s\n%s\n%s\n", headers[0], headers[1], headers[2], headers[3]);
    
    // printf("%s\n", buffer);

    return 0;
}