#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>
#include "turbojpeg.h"

#define WIDTH 1280
#define HEIGHT 720
#define REQ_BUFFER_COUNT 4
#define PIXEL_THRESHOLD 30
#define TRIGGER_THRESHOLD 50000


void stuur_foto_naar_internet(
    unsigned char *jpeg_data,
    int jpeg_grootte
) {
    CURL *curl = curl_easy_init();

    if (!curl) {
        printf("❌ [Internet] CURL kon niet worden gestart.\n");
        return;
    }

    const char *bot_token = getenv("TELEGRAM_BOT_TOKEN");
    const char *chat_id = getenv("TELEGRAM_CHAT_ID");

    if (!bot_token || !chat_id) {
        printf("❌ [Internet] TELEGRAM_BOT_TOKEN of TELEGRAM_CHAT_ID ontbreekt.\n");
        curl_easy_cleanup(curl);
        return;
    }

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "https://api.telegram.org/bot%s/sendPhoto",
        bot_token
    );

    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;

    // Chat ID
    curl_formadd(
        &formpost,
        &lastptr,
        CURLFORM_COPYNAME, "chat_id",
        CURLFORM_COPYCONTENTS, chat_id,
        CURLFORM_END
    );

    // Caption
    curl_formadd(
        &formpost,
        &lastptr,
        CURLFORM_COPYNAME, "caption",
        CURLFORM_COPYCONTENTS, "🚨 Beweging gedetecteerd!",
        CURLFORM_END
    );

    // JPEG
    curl_formadd(
        &formpost,
        &lastptr,
        CURLFORM_COPYNAME, "photo",
        CURLFORM_BUFFER, "motion.jpeg",
        CURLFORM_BUFFERPTR, jpeg_data,
        CURLFORM_BUFFERLENGTH, (long)jpeg_grootte,
        CURLFORM_CONTENTTYPE, "image/jpeg",
        CURLFORM_END
    );

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

    // Timeout zodat je camera niet eindeloos blijft hangen
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    printf(
        "🌐 [Internet] 📸 Foto van %d bytes verzenden... ",
        jpeg_grootte
    );
    fflush(stdout);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {

        long response_code = 0;

        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &response_code
        );

        if (response_code >= 200 && response_code < 300) {
            printf("✅ Verzonden! HTTP %ld\n", response_code);
        } else {
            printf("⚠️ Telegram gaf HTTP %ld\n", response_code);
        }

    } else {
        printf(
            "❌ Mislukt: %s\n",
            curl_easy_strerror(res)
        );
    }

    curl_formfree(formpost);
    curl_easy_cleanup(curl);
}

int main() {
    
    int fd = open("/dev/video0", O_RDWR);
    if (fd == -1) { 
        perror("Camera openen mislukt!");
        return 1; 
    }

    struct v4l2_format fmt = {0};

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("YUYV 720p instellen mislukt. Ondersteunt je webcam dit?");
        close(fd);
        return 1;
    }

    struct v4l2_requestbuffers req = {0};
    req.count = REQ_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);

    void* buffer_starts[REQ_BUFFER_COUNT];
    size_t buffer_lengths[REQ_BUFFER_COUNT];

    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        buffer_lengths[i] = buf.length;
        buffer_starts[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    int totaal_pixels = WIDTH * HEIGHT;
    unsigned char *vorig_frame = malloc(totaal_pixels);
    if (!vorig_frame) { perror("Malloc mislukt"); return 1; }

    memset(vorig_frame, 0, totaal_pixels);
    int eerste_frame = 1;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);
    curl_global_init(CURL_GLOBAL_ALL);

    printf("\n🤖 LIVE BEWEGINGSMELDER ACTIEF (Ctrl+C om te stoppen)...\n\n");

    while(1) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        // printf("%p", (void *)&buf);
        if (ioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
            // perror("er is een probleem!");
            continue;
        }

        unsigned char *huidig_yuyv = (unsigned char *)buffer_starts[buf.index];
        int bewegende_pixels = 0;

        for (int i = 0; i < totaal_pixels; i++) {
            int motion = abs(vorig_frame[i] - huidig_yuyv[i * 2]);
            if (motion > PIXEL_THRESHOLD) {
                bewegende_pixels++;
            }

            vorig_frame[i] = huidig_yuyv[i * 2];
        }

        if (eerste_frame) {
            printf("[System] Eerste frame opgevangen om als referentie te dienen.\n");
            eerste_frame = 0;
        } else {
            if (bewegende_pixels > TRIGGER_THRESHOLD) {
                printf("🚨 [MOTION] Getriggerd! %d pixels veranderd.\n", bewegende_pixels);
            } else {
                printf("🍃 [RUIS] Geen significante beweging (%d pixels).\n", bewegende_pixels);
            }
        }

        ioctl(fd, VIDIOC_QBUF, &buf);

        // usleep(100000);
    }


    ioctl(fd, VIDIOC_STREAMOFF, &type);
    free(vorig_frame);
    close(fd);
    
    return 0;
}