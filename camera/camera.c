#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>      // Nodig voor open()
#include <unistd.h>     // Nodig voor close()
#include <sys/ioctl.h>  // Nodig voor ioctl()
#include <sys/mman.h>
#include <linux/videodev2.h> // De V4L2 header

int main() {
    int fd = open("/dev/video0", O_RDWR);
    if (fd == -1) {
        perror("Kan de camera niet openen!");
        return 1;
    }
    printf("Succes: Camera geopend! (File Descriptor: %d)\n", fd);

    struct v4l2_format fmt = {0};

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt)) {
        perror("Formaat instellen mislukt");
        close(fd);
        return 1;
    }

    struct v4l2_requestbuffers req = {0};
    
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("Buffer aanvraag mislukt");
        close(fd);
        return 1;
    }

    struct v4l2_buffer buf = {0};

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
        perror("Query buffer mislukt!");
        close(fd);
        return 1;
    }

    void* buffer_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer_start == MAP_FAILED) {
        perror("mmap mislukt");
        close(fd);
        return 1;
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        perror("Queue buffer mislukt!");
        return 1;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Stream starten mislukt");
        return 1;
    }

    printf("Camera is aan het 'kijken'... Frame opvangen...\n");

    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) { 
        perror("Frame opvangen mislukt");
        return 1;
    }

    FILE *file = fopen("images/foto.jpg", "wb");

    if (file) {
        fwrite(buffer_start, buf.bytesused, 1, file);
        fclose(file);
        printf("Succes! Foto opgeslagen!\n");
    } else {
        perror("Bestand opslaan mislukt!");
    }

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(buffer_start, buf.length);
    close(fd);
    
    return 0;
}