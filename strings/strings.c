#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main() {
    char voornaam[] = "Karim";
    char kopy[10];

    strcpy(kopy, voornaam);

    if(strcmp(voornaam, kopy) == 0) {
        printf("Het is gelukt\n");
    }

    return 0;
}