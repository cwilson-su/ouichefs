#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OUICHEFS_IOC_DEFRAG_FILE _IO('O', 2)

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <file_to_defrag>\n", argv[0]);
        return 1;
    }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Error opening file");
        return 1;
    }
    if (ioctl(fd, OUICHEFS_IOC_DEFRAG_FILE) < 0) {
        perror("Error during defragmentation");
    } else {
        printf("Defragmentation command sent successfully!\n");
    }
    close(fd);
    return 0;
}
