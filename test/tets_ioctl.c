#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>                                                        
#include <unistd.h>
#include <sys/ioctl.h>                                                      
#include <errno.h>
#include <string.h>

#define OUICHEFS_IOC_GET_EXTENTS _IO('O', 1)

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <chemin_du_fichier_ouichefs>\n", argv[0]);       
        return EXIT_FAILURE;
    }
    
    int fd = open(argv[1], O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "Erreur lors de l'ouverture de %s\n", argv[1]);  
        return EXIT_FAILURE;
    }

    if (ioctl(fd, OUICHEFS_IOC_GET_EXTENTS) < 0) {
        fprintf(stderr, "Erreur ioctl : %s\n", strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}