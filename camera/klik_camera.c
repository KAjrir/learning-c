#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <termios.h>    // Nodig voor terminal-magie (raw mode)
#include <sys/select.h> // Nodig om te kijken of er een toets is ingedrukt
#include <sys/time.h>   //  Nodig voor de 'struct timeval' (de timer)

#define REQ_BUFFER_COUNT 1

// 👨‍🏫 Mentor-hulpfunctie: Luisteren naar toetsenbord zonder te blokkeren
int kbhit()
{
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

int main()
{
    // --- TERMINAL SETUP (Raw Mode inschakelen) ---
    struct termios orig_termios, raw_termios;
    tcgetattr(STDIN_FILENO, &orig_termios); // Onthoud oude terminal-instellingen
    raw_termios = orig_termios;
    raw_termios.c_lflag &= ~(ICANON | ECHO); // Zet Enter-verplichting en tekst-echo uit
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);

    // --- CAMERA SETUP (De bekende stappen) ---
    int fd = open("/dev/video0", O_RDWR);
    if (fd == -1)
    {
        perror("Camera openen mislukt");
        goto cleanup_terminal;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 1280;
    fmt.fmt.pix.height = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
    {
        perror("Formaat mislukt");
        goto cleanup_camera;
    }

    struct v4l2_requestbuffers req = {0};
    req.count = REQ_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
    {
        perror("Buffers aanvragen mislukt");
        goto cleanup_camera;
    }

    void *buffer_starts[REQ_BUFFER_COUNT];
    size_t buffer_lengths[REQ_BUFFER_COUNT];

    for (int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        buffer_lengths[i] = buf.length;
        buffer_starts[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    // --- DE KRAAN STAAT WEER CONSTANT AAN (Voor de live preview) ---
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    printf("\n📸 LIVE STREAM DRAAIT IN HET RAM!\n");
    printf("--> Druk op [SPATIEBALK] om NU een actueel frame te bevriezen.\n");
    printf("--> Druk op [Q] om te stoppen.\n\n");

    int foto_teller = 1;
    int keep_running = 1;

    // --- DE LIVE STREAM LOOP ---
    while (keep_running) {
        
        // 1. We halen CONTINU de allernieuwste emmer van de lopende band.
        // Omdat we dit non-stop doen, blijft de stream 100% actueel!
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        
        // DQBUF wacht heel even (max 1/30e sec) tot de camera een vers frame klaar heeft
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            int i = buf.index; // Dit is de actuele emmer van DIT milliseconde!

            // 2. We kijken of de gebruiker toevallig NU op de knop drukt
            if (kbhit()) {
                char ch = getchar();

                if (ch == ' ') { // CAPTURE!
                    // Omdat we de emmer NU in handen hebben, bevriezen we hem naar schijf!
                    char bestandsnaam[32];
                    sprintf(bestandsnaam, "images/foto-%d.jpg", foto_teller);

                    FILE *file = fopen(bestandsnaam, "wb");
                    if (file) {
                        fwrite(buffer_starts[i], buf.bytesused, 1, file);
                        fclose(file);
                        printf("📸 CAPTURE! Frame %d opgeslagen als '%s'\n", i, bestandsnaam);
                        foto_teller++;
                    }
                } else if (ch == 'q' || ch == 'Q') {
                    keep_running = 0;
                }
            }

            // 3. BELANGRIJK: Gooi de emmer direct weer terug op de lopende band.
            // Of we nou een foto hebben opgeslagen of niet, de band moet blijven draaien!
            ioctl(fd, VIDIOC_QBUF, &buf);
        }
        
        // Geen usleep() meer nodig! ioctl(DQBUF) remt de loop al automatisch af 
        // tot de exacte snelheid van de camera (bijv. 30 keer per seconde).
    }



    printf("\n🛑 Programma sluiten en opruimen...\n");

    // --- CLEANUP SECTIE ---
    ioctl(fd, VIDIOC_STREAMOFF, &type);
cleanup_mmap:
    for (int i = 0; i < req.count; i++)
    {
        munmap(buffer_starts[i], buffer_lengths[i]);
    }
cleanup_camera:
    close(fd);
cleanup_terminal:
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); // Zet de terminal weer netjes terug naar normaal!
    return 0;
}
