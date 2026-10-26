#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define REQ_BUFFER_COUNT 8
#define TOTAL_FRAMES 150

int main() {
    // [STAP 1 t/m 5 zijn EXACT hetzelfde als je vorige code: open, format, reqbufs, querybuf, mmap, qbuf]
    int fd = open("/dev/video0", O_RDWR);
    if (fd == -1) { perror("Camera openen mislukt"); return 1; }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) { perror("Format mislukt"); return 1; }

    struct v4l2_requestbuffers req = {0};
    req.count = REQ_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) { perror("Reqbufs mislukt"); return 1; }

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

    // =========================================================================
    // 🛠️ DE MP4 MAGIE BEGINT HIER: We openen FFmpeg als een actieve pijpleiding
    // =========================================================================
    // Dit commando zegt: FFmpeg, er komt een stroom MJPEG frames binnen via 'stdin' 
    // met 30 frames per seconde en resolutie 640x480. Maak er een H.264 MP4 van!
    // We zetten de eigenschappen van de INPUT nu STRENG vóór de '-i -' (input pipe)
    char *cmd = "ffmpeg -y -f mjpeg 30 -i - -vcodec libx264 -pix_fmt yuv420p mijn_video.mp4";

    
    // popen() werkt net als fopen(), maar start een proces in plaats van een bestand
    FILE *ffmpeg_pipe = popen(cmd, "w");
    if (!ffmpeg_pipe) {
        perror("Kan FFmpeg pijpleiding niet openen. Staat ffmpeg geïnstalleerd?");
        close(fd);
        return 1;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    printf("\n🎬 We nemen nu op RECHTSTREEKS naar 'mijn_video.mp4'...\n");

    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ioctl(fd, VIDIOC_DQBUF, &buf);

        // In plaats van fwrite naar een bestand, schrijven we nu in de PIJPLEIDING naar FFmpeg!
        fwrite(buffer_starts[buf.index], buf.bytesused, 1, ffmpeg_pipe);

        printf("\rFrame %d/%d verwerkt door MP4-encoder...", frame + 1, TOTAL_FRAMES);
        fflush(stdout);

        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    printf("\n\n🛑 Opname klaar! MP4 bestand afronden...\n");

    // Netjes sluiten
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // pclose() zorgt dat FFmpeg de video netjes uitschrijft en stopt
    pclose(ffmpeg_pipe); 

    for (int i = 0; i < req.count; i++) {
        munmap(buffer_starts[i], buffer_lengths[i]);
    }
    close(fd);

    printf("Succes! Je hebt een échte 'mijn_video.mp4' gemaakt!\n");
    return 0;
}
