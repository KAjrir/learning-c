#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

// We gebruiken een bekende open-source JPEG decoder voor C
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#define PIXEL_THRESHOLD 30
#define TRIGGER_THRESHOLD 90000

// Mentor-hulpfunctie om een JPEG direct als grijswaarden in te laden
unsigned char* laad_grijswaarden_jpeg(const char* bestandsnaam, int* breedte, int* hoogte) {
    int kanalen;
    // De '1' aan het einde dwingt STB af om het direct naar 1 byte per pixel (grijs) om te zetten!
    unsigned char* data = stbi_load(bestandsnaam, breedte, hoogte, &kanalen, 1);
    if (!data) {
        printf("❌ Fout: Kan %s niet openen. Bestaat het bestand wel?\n", bestandsnaam);
    }
    return data;
}

int main() {
    int w1, h1, w2, h2;
    // 1. Laad de basis (referentie) foto
    unsigned char* basis_frame = laad_grijswaarden_jpeg("images/basis.jpg", &w1, &h1);
    // 2. Laad de test foto (wissel hier straks tussen weinig_beweging.jpg en veel_beweging.jpg)
    unsigned char* nieuw_frame = laad_grijswaarden_jpeg("images/veel_beweging.jpg", &w2, &h2);
    
    printf("%d", w1 * h1);
    if (!basis_frame || !nieuw_frame) return 1;
    if (w1 != w2 || h1 != h2) {
        printf("❌ Fout: De foto's moeten exact dezelfde resolutie hebben!\n");
        return 1;
    }

    int totaal_pixels = w1 * h1;
    printf("📊 Foto's succesvol geladen. Resolutie: %dx%d (%d pixels totaal).\n", w1, h1, totaal_pixels);

    // Het aantal pixels dat significant is veranderd
    int bewegende_pixels = 0;

    // =========================================================================
    // 🛠️ JOUW CODEER-ZONE: VERGELIJK HIER ELKE PIXEL
    // =========================================================================
    
    // [HIER GA JIJ DE FOR-LOOP SCHRIJVEN]
    // Hint: Loop van p = 0 tot totaal_pixels.
    // Vergelijk basis_frame[p] met nieuw_frame[p].

    struct timespec start, eind;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < totaal_pixels; i++) {
        int motion = abs(basis_frame[i] - nieuw_frame[i]);
        if (motion > PIXEL_THRESHOLD) {
            bewegende_pixels++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &eind);

    double tijd_sec = (eind.tv_sec - start.tv_sec) + 
                      (eind.tv_nsec - start.tv_nsec) / 1000000000.0;
    double tijd_ms = tijd_sec * 1000.0;

    printf("\n⏱️ Performance resultaat:\n");
    printf("   De for-loop nam %.3f milliseconden in beslag voor %d pixels.\n\n", tijd_ms, totaal_pixels);


    // =========================================================================

    // 4. Evalueer het resultaat
    printf("🔎 Aantal bewegende pixels gedetecteerd: %d\n", bewegende_pixels);
    
    if (bewegende_pixels > TRIGGER_THRESHOLD) {
        printf("🚨 TRIGGER! Significante beweging gedetecteerd! (Auto/Mens)\n");
    } else {
        printf("🍃 RUIS. Geen grote beweging gedetecteerd (of bladeren/wind).\n");
    }

    // Geheugen netjes opruimen
    stbi_image_free(basis_frame);
    stbi_image_free(nieuw_frame);
    return 0;
}
