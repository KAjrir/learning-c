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

#define REQ_BUFFER_COUNT 8  // De 'sweet spot' aantal emmers op de lopende band
#define TOTAL_FRAMES 150    // 150 frames / 30 fps = 5 seconden video

int main() {
    // 1. Voordeur openen
    int fd = open("/dev/video0", O_RDWR);
    if (fd == -1) { perror("Kan camera niet openen"); return 1; }

    // 2. Formaat instellen (640x480 MJPEG)
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Formaat instellen mislukt");
        close(fd);
        return 1;
    }

    // 3. Vraag om 8 emmers (buffers) op de lopende band
    struct v4l2_requestbuffers req = {0};
    req.count = REQ_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("Buffer aanvraag mislukt");
        close(fd);
        return 1;
    }

    printf("[Mentor Info] Gereserveerde emmers door de driver: %d\n", req.count);

    // Arrays maken om de 8 tunnels (pointers) en lengtes bij te houden
    void* buffer_starts[REQ_BUFFER_COUNT];
    size_t buffer_lengths[REQ_BUFFER_COUNT];

    // 4 & 5. Loop om de tunnels te graven en de emmers alvast klaar te zetten
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // Vraag locatie van emmer 'i' op
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("Query buffer mislukt");
            close(fd);
            return 1;
        }

        buffer_lengths[i] = buf.length;

        // Graaf de tunnel naar emmer 'i' middels de offset!
        buffer_starts[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffer_starts[i] == MAP_FAILED) {
            perror("mmap mislukt");
            close(fd);
            return 1;
        }

        // Zet de lege emmer 'i' alvast in de wachtrij onder de kraan
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Queue buffer mislukt");
            close(fd);
            return 1;
        }
    }

    // 6. Open het videobestand waarin we alle frames achter elkaar gaan dumpen
    FILE *video_file = fopen("output_video.avi", "wb");
    if (!video_file) { perror("Kan videobestand niet aanmaken"); close(fd); return 1; }

    // 7. De kraan opendraaien (Stream aan!)
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Stream starten mislukt");
        fclose(video_file);
        close(fd);
        return 1;
    }

    printf("\n🎬 CAMERA IS AAN! We nemen NU 5 seconden video op...\n");

    // 8. De streaming loop: 150 frames lang bordjes van de lopende band pakken
    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        // A. Haal het EERSTVOLGENDE VOLLE bordje van de lopende band
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Frame DQBUF mislukt");
            break;
        }

        // B. Pak de index: welke emmer heeft de camera zojuist gevuld?
        int i = buf.index;

        // C. Schrijf de JPEG-bytes van dít frame direct achter elkaar in ons videobestand
        fwrite(buffer_starts[i], buf.bytesused, 1, video_file);
        
        // Een kleine visuele indicator in de terminal zodat je weet dat hij bezig is
        printf("\rFrame %d/%d opgenomen (Emmer %d gebruikt)...", frame + 1, TOTAL_FRAMES, i);
        fflush(stdout);

        // D. BELANGRIJK: Zet het lege bordje weer TERUG op de band zodat de camera hem weer kan vullen
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Frame QBUF mislukt");
            break;
        }
    }

    printf("\n\n🛑 Opname klaar! We sluiten de boel netjes af...\n");

    // 9. Netjes opruimen
    ioctl(fd, VIDIOC_STREAMOFF, &type); // Kraan dicht
    fclose(video_file);                 // Bestand opslaan/sluiten

    // Alle 8 tunnels instorten
    for (int i = 0; i < req.count; i++) {
        munmap(buffer_starts[i], buffer_lengths[i]);
    }
    
    close(fd); // Voordeur op slot
    printf("Succes! Video opgeslagen als 'output_video.avi'\n");

    return 0;
}
