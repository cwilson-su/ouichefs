### 1.1 (A refaire à chaque modification du module)

# compilation 

make → ouichefs.ko
insmod ouichefs.ko
cd mkfs/
make → mfs.ouichefs
make img → test.img (!!! modifier IMGSIZE en fonction de df -h .)

# Montage

mkdir mnt
mount -o loop -t ouichefs test.img mnt

# Démontage

umount mnt
rmmod ouichefs

### 1.2

voir file.c + Test_1_2.txt

ouichefs_read → Test validé

ouichefs_write → Test validé

différence de performance : Avant : / Après :

### 1.3

voir ouichefs.h

struct ouichefs_extent + modif ouichefs_file_index_block

voir file.c

ouichefs_ioctl → A tester