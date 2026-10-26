#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <curl/curl.h>
#include <turbojpeg.h>


/* ============================================================
   CAMERA
   ============================================================ */

#define DEVICE "/dev/video0"

#define WIDTH 1280
#define HEIGHT 720

#define REQ_BUFFER_COUNT 4


/* ============================================================
   MOTION DETECTION
   ============================================================ */

#define PIXEL_THRESHOLD 30
#define TRIGGER_THRESHOLD 50000


/* ============================================================
   JPEG
   ============================================================ */

#define JPEG_QUALITY 80


/* ============================================================
   TELEGRAM
   ============================================================ */

/*
 * Token en chat-id komen uit environment variables:
 *
 *   export TELEGRAM_BOT_TOKEN="..."
 *   export TELEGRAM_CHAT_ID="..."
 */
static const char *TELEGRAM_BOT_TOKEN = NULL;

static const char *TELEGRAM_CHAT_ID = NULL;


/* ============================================================
   ioctl helper
   ============================================================ */

static int xioctl(
    int fd,
    unsigned long request,
    void *arg
) {
    int result;

    do {
        result = ioctl(fd, request, arg);
    } while (result == -1 && errno == EINTR);

    return result;
}


/* ============================================================
   YUYV -> JPEG
   ============================================================ */

static unsigned char *yuyv_to_jpeg(
    const unsigned char *yuyv,
    int width,
    int height,
    int quality,
    unsigned long *jpeg_size
) {
    *jpeg_size = 0;

    /*
     * TurboJPEG encoder starten
     */

    tjhandle handle = tjInitCompress();

    if (!handle) {
        fprintf(
            stderr,
            "❌ tjInitCompress(): %s\n",
            tjGetErrorStr()
        );

        return NULL;
    }


    /*
     * YUYV is:
     *
     * Y0 U0 Y1 V0
     * Y2 U1 Y3 V1
     *
     * TurboJPEG verwacht bij
     * tjCompressFromYUVPlanes():
     *
     * Y plane
     * U plane
     * V plane
     *
     * Daarom splitsen we YUYV hier op.
     */


    size_t y_size =
        (size_t)width * height;

    size_t uv_size =
        (size_t)(width / 2) * height;


    unsigned char *y_plane =
        malloc(y_size);

    unsigned char *u_plane =
        malloc(uv_size);

    unsigned char *v_plane =
        malloc(uv_size);


    if (!y_plane ||
        !u_plane ||
        !v_plane) {

        fprintf(
            stderr,
            "❌ YUV buffers alloceren mislukt.\n"
        );

        free(y_plane);
        free(u_plane);
        free(v_plane);

        tjDestroy(handle);

        return NULL;
    }


    /*
     * YUYV -> YUV planes
     */

    for (int y = 0; y < height; y++) {

        const unsigned char *src =
            yuyv +
            (size_t)y * width * 2;


        unsigned char *dst_y =
            y_plane +
            (size_t)y * width;


        unsigned char *dst_u =
            u_plane +
            (size_t)y * (width / 2);


        unsigned char *dst_v =
            v_plane +
            (size_t)y * (width / 2);


        for (int x = 0; x < width; x += 2) {

            int src_index =
                x * 2;

            int uv_index =
                x / 2;


            /*
             * Y0
             */

            dst_y[x] =
                src[src_index];


            /*
             * Y1
             */

            dst_y[x + 1] =
                src[src_index + 2];


            /*
             * U
             */

            dst_u[uv_index] =
                src[src_index + 1];


            /*
             * V
             */

            dst_v[uv_index] =
                src[src_index + 3];
        }
    }


    /*
     * TurboJPEG planes
     */

    const unsigned char *planes[3] = {
        y_plane,
        u_plane,
        v_plane
    };


    /*
     * Bytes per row van iedere plane
     */

    int strides[3] = {
        width,
        width / 2,
        width / 2
    };


    /*
     * TurboJPEG maakt de JPEG-buffer
     * voor ons aan.
     */

    unsigned char *jpeg_data = NULL;

    unsigned long jpeg_capacity = 0;


    int result =
        tjCompressFromYUVPlanes(
            handle,

            planes,

            width,

            strides,

            height,

            TJSAMP_422,

            &jpeg_data,

            &jpeg_capacity,

            quality,

            TJFLAG_FASTDCT
        );


    /*
     * YUV tijdelijke buffers kunnen weg.
     */

    free(y_plane);
    free(u_plane);
    free(v_plane);


    if (result != 0) {

        fprintf(
            stderr,
            "❌ JPEG encoding mislukt: %s\n",
            tjGetErrorStr()
        );

        tjDestroy(handle);

        return NULL;
    }


    tjDestroy(handle);


    /*
     * Dit is de JPEG pointer die de caller krijgt.
     */

    *jpeg_size =
        jpeg_capacity;


    return jpeg_data;
}


/* ============================================================
   TELEGRAM
   ============================================================ */

static void stuur_foto_naar_internet(
    unsigned char *jpeg_data,
    unsigned long jpeg_grootte
) {
    CURL *curl =
        curl_easy_init();


    if (!curl) {

        fprintf(
            stderr,
            "❌ CURL kon niet worden gestart.\n"
        );

        return;
    }


    /*
     * Telegram API URL
     */

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "https://api.telegram.org/bot%s/sendPhoto",
        TELEGRAM_BOT_TOKEN
    );


    /*
     * Moderne CURL MIME API
     */

    curl_mime *mime =
        curl_mime_init(curl);


    if (!mime) {

        fprintf(
            stderr,
            "❌ CURL MIME kon niet worden aangemaakt.\n"
        );

        curl_easy_cleanup(curl);

        return;
    }


    /*
     * --------------------------------------------------------
     * chat_id
     * --------------------------------------------------------
     */

    curl_mimepart *part =
        curl_mime_addpart(mime);


    curl_mime_name(
        part,
        "chat_id"
    );


    curl_mime_data(
        part,
        TELEGRAM_CHAT_ID,
        CURL_ZERO_TERMINATED
    );


    /*
     * --------------------------------------------------------
     * caption
     * --------------------------------------------------------
     */

    part =
        curl_mime_addpart(mime);


    curl_mime_name(
        part,
        "caption"
    );


    curl_mime_data(
        part,
        "🚨 Beweging gedetecteerd!",
        CURL_ZERO_TERMINATED
    );


    /*
     * --------------------------------------------------------
     * JPEG
     * --------------------------------------------------------
     */

    part =
        curl_mime_addpart(mime);


    curl_mime_name(
        part,
        "photo"
    );


    /*
     * JPEG zit al in RAM.
     *
     * CURL hoeft geen bestand te openen.
     */

    curl_mime_data(
        part,
        (const char *)jpeg_data,
        jpeg_grootte
    );


    curl_mime_filename(
        part,
        "motion.jpg"
    );


    curl_mime_type(
        part,
        "image/jpeg"
    );


    /*
     * --------------------------------------------------------
     * CURL instellingen
     * --------------------------------------------------------
     */

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url
    );


    curl_easy_setopt(
        curl,
        CURLOPT_MIMEPOST,
        mime
    );


    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT,
        5L
    );


    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT,
        15L
    );


    /*
     * --------------------------------------------------------
     * Verzenden
     * --------------------------------------------------------
     */

    printf(
        "🌐 📸 %lu bytes naar Telegram...",
        jpeg_grootte
    );

    fflush(stdout);


    CURLcode result =
        curl_easy_perform(curl);


    if (result != CURLE_OK) {

        printf(
            " ❌ %s\n",
            curl_easy_strerror(result)
        );

    } else {

        long http_code = 0;


        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &http_code
        );


        if (http_code >= 200 &&
            http_code < 300) {

            printf(
                " ✅ Verzonden! HTTP %ld\n",
                http_code
            );

        } else {

            printf(
                " ⚠️ Telegram HTTP %ld\n",
                http_code
            );
        }
    }


    curl_mime_free(mime);

    curl_easy_cleanup(curl);
}


/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    printf(
        "\n"
        "========================================\n"
        "   🤖 LIVE MOTION DETECTOR\n"
        "========================================\n"
        "\n"
    );


    /* ========================================================
       Telegram configuratie uit environment
       ======================================================== */

    TELEGRAM_BOT_TOKEN = getenv("TELEGRAM_BOT_TOKEN");
    TELEGRAM_CHAT_ID = getenv("TELEGRAM_CHAT_ID");

    if (!TELEGRAM_BOT_TOKEN || !TELEGRAM_CHAT_ID) {

        fprintf(
            stderr,
            "❌ Zet TELEGRAM_BOT_TOKEN en TELEGRAM_CHAT_ID als environment variables.\n"
        );

        return 1;
    }


    /* ========================================================
       CURL initialiseren
       ======================================================== */

    if (curl_global_init(
            CURL_GLOBAL_DEFAULT
        ) != 0) {

        fprintf(
            stderr,
            "❌ curl_global_init() mislukt.\n"
        );

        return 1;
    }


    /* ========================================================
       CAMERA OPENEN
       ======================================================== */

    int fd =
        open(
            DEVICE,
            O_RDWR
        );


    if (fd == -1) {

        perror(
            "❌ Camera openen"
        );

        curl_global_cleanup();

        return 1;
    }


    printf(
        "📷 Camera: %s\n",
        DEVICE
    );


    /* ========================================================
       CAMERA FORMAT
       ======================================================== */

    struct v4l2_format fmt = {0};


    fmt.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;


    fmt.fmt.pix.width =
        WIDTH;


    fmt.fmt.pix.height =
        HEIGHT;


    fmt.fmt.pix.pixelformat =
        V4L2_PIX_FMT_YUYV;


    fmt.fmt.pix.field =
        V4L2_FIELD_NONE;


    if (xioctl(
            fd,
            VIDIOC_S_FMT,
            &fmt
        ) == -1) {

        perror(
            "❌ VIDIOC_S_FMT"
        );

        close(fd);
        curl_global_cleanup();

        return 1;
    }


    printf(
        "🎥 Resolutie: %ux%u\n",
        fmt.fmt.pix.width,
        fmt.fmt.pix.height
    );


    printf(
        "📦 Pixel format: %.4s\n",
        (char *)&fmt.fmt.pix.pixelformat
    );


    if (fmt.fmt.pix.pixelformat !=
        V4L2_PIX_FMT_YUYV) {

        fprintf(
            stderr,
            "❌ Camera heeft YUYV niet geaccepteerd.\n"
        );

        close(fd);
        curl_global_cleanup();

        return 1;
    }


    /* ========================================================
       V4L2 BUFFERS
       ======================================================== */

    struct v4l2_requestbuffers req = {0};


    req.count =
        REQ_BUFFER_COUNT;


    req.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;


    req.memory =
        V4L2_MEMORY_MMAP;


    if (xioctl(
            fd,
            VIDIOC_REQBUFS,
            &req
        ) == -1) {

        perror(
            "❌ VIDIOC_REQBUFS"
        );

        close(fd);
        curl_global_cleanup();

        return 1;
    }


    void *buffer_starts[
        REQ_BUFFER_COUNT
    ];


    size_t buffer_lengths[
        REQ_BUFFER_COUNT
    ];


    memset(
        buffer_starts,
        0,
        sizeof(buffer_starts)
    );


    /* ========================================================
       MMAP BUFFERS
       ======================================================== */

    for (unsigned int i = 0;
         i < req.count;
         i++) {

        struct v4l2_buffer buf = {0};


        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE;


        buf.memory =
            V4L2_MEMORY_MMAP;


        buf.index =
            i;


        if (xioctl(
                fd,
                VIDIOC_QUERYBUF,
                &buf
            ) == -1) {

            perror(
                "❌ VIDIOC_QUERYBUF"
            );

            close(fd);
            curl_global_cleanup();

            return 1;
        }


        buffer_lengths[i] =
            buf.length;


        buffer_starts[i] =
            mmap(
                NULL,
                buf.length,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                buf.m.offset
            );


        if (buffer_starts[i] ==
            MAP_FAILED) {

            perror("❌ mmap");

            close(fd);
            curl_global_cleanup();

            return 1;
        }


        if (xioctl(
                fd,
                VIDIOC_QBUF,
                &buf
            ) == -1) {

            perror(
                "❌ VIDIOC_QBUF"
            );

            close(fd);
            curl_global_cleanup();

            return 1;
        }
    }


    /* ========================================================
       MOTION BUFFER
       ======================================================== */

    const size_t total_pixels =
        (size_t)WIDTH * HEIGHT;


    unsigned char *vorig_frame =
        malloc(total_pixels);


    if (!vorig_frame) {

        perror(
            "❌ vorig_frame malloc"
        );

        close(fd);
        curl_global_cleanup();

        return 1;
    }


    memset(
        vorig_frame,
        0,
        total_pixels
    );


    int eerste_frame = 1;


    /* ========================================================
       CAMERA STREAM STARTEN
       ======================================================== */

    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;


    if (xioctl(
            fd,
            VIDIOC_STREAMON,
            &type
        ) == -1) {

        perror(
            "❌ VIDIOC_STREAMON"
        );

        free(vorig_frame);
        close(fd);
        curl_global_cleanup();

        return 1;
    }


    printf(
        "\n"
        "🚀 Motion detector actief!\n"
        "\n"
        "   Resolutie : %dx%d\n"
        "   Format    : YUYV\n"
        "   Y threshold: %d\n"
        "   Trigger   : %d pixels\n"
        "   JPEG      : %d%%\n"
        "\n"
        "Ctrl+C om te stoppen.\n"
        "\n",

        WIDTH,
        HEIGHT,
        PIXEL_THRESHOLD,
        TRIGGER_THRESHOLD,
        JPEG_QUALITY
    );


    /* ========================================================
       MAIN LOOP
       ======================================================== */

    while (1) {

        struct v4l2_buffer buf = {0};


        buf.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE;


        buf.memory =
            V4L2_MEMORY_MMAP;


        /*
         * Volgend cameraframe ophalen.
         */

        if (xioctl(
                fd,
                VIDIOC_DQBUF,
                &buf
            ) == -1) {

            if (errno == EINTR)
                continue;


            perror(
                "❌ VIDIOC_DQBUF"
            );

            break;
        }


        /*
         * Pointer naar het echte
         * cameraframe.
         */

        unsigned char *huidig_yuyv =
            buffer_starts[buf.index];


        int bewegende_pixels = 0;


        /* ====================================================
           MOTION DETECTION

           YUYV:

           Y U Y V Y U Y V
           ↑   ↑   ↑   ↑
           ==================================================== */

        for (size_t i = 0;
             i < total_pixels;
             i++) {

            unsigned char current_y =
                huidig_yuyv[i * 2];


            int difference =
                abs(
                    (int)current_y -
                    (int)vorig_frame[i]
                );


            if (difference >
                PIXEL_THRESHOLD) {

                bewegende_pixels++;
            }


            vorig_frame[i] =
                current_y;
        }


        /* ====================================================
           EERSTE FRAME
           ==================================================== */

        if (eerste_frame) {

            printf(
                "🎬 Eerste frame ontvangen.\n"
            );

            eerste_frame = 0;
        }


        /* ====================================================
           MOTION TRIGGER
           ==================================================== */

        else if (
            bewegende_pixels >
            TRIGGER_THRESHOLD
        ) {

            printf(
                "\n"
                "🚨 MOTION!\n"
                "   %d pixels veranderd.\n",
                bewegende_pixels
            );


            /* =================================================
               YUYV → JPEG
               ================================================= */

            unsigned long jpeg_size = 0;


            unsigned char *jpeg_data =
                yuyv_to_jpeg(
                    huidig_yuyv,
                    WIDTH,
                    HEIGHT,
                    JPEG_QUALITY,
                    &jpeg_size
                );


            if (!jpeg_data) {

                fprintf(
                    stderr,
                    "❌ JPEG encoding mislukt.\n"
                );

            } else {

                printf(
                    "🖼️ JPEG gemaakt: %lu bytes\n",
                    jpeg_size
                );


                /* =============================================
                   JPEG → TELEGRAM
                   ============================================= */

                stuur_foto_naar_internet(
                    jpeg_data,
                    jpeg_size
                );


                /*
                 * TurboJPEG heeft de JPEG-buffer
                 * gealloceerd.
                 */

                tjFree(jpeg_data);
            }
        }


        /* ====================================================
           BUFFER TERUG NAAR CAMERA
           ==================================================== */

        if (xioctl(
                fd,
                VIDIOC_QBUF,
                &buf
            ) == -1) {

            perror(
                "❌ VIDIOC_QBUF"
            );

            break;
        }
    }


    /* ========================================================
       CLEANUP
       ======================================================== */

    xioctl(
        fd,
        VIDIOC_STREAMOFF,
        &type
    );


    for (unsigned int i = 0;
         i < req.count;
         i++) {

        if (buffer_starts[i] !=
            MAP_FAILED) {

            munmap(
                buffer_starts[i],
                buffer_lengths[i]
            );
        }
    }


    free(vorig_frame);

    close(fd);

    curl_global_cleanup();


    return 0;
}