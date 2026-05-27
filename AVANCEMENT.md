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

voir file.c + test_1_2.txt

ouichefs_read → Test validé

ouichefs_write → Test validé

différence de performance : Avant : / Après :

### 1.3

voir ouichefs.h

struct ouichefs_extent + modif ouichefs_file_index_block

voir file.c

ouichefs_ioctl → Test validé

test_ioctl.c (ajout)
gcc -o test_ioctl test_ioctl.c

### 1.4

voir file.c

ouichefs_read (modifié) → Test validé

ouichefs_extent_get_block → Test validé

voir inode.c

ouichefs_unlink (modifié)

### 1.5

voir file.c + test_1_5_3.txt

ouichefs_write (modifié) → Test validé

ouichefs_file_get_block (modifié) → Test validé

voir inode.c

ouichefs_unlink (Déjà modifié dans 1.4) 

### 1.6

voir Bitmap.h

ouichefs_alloc_contiguous → Test validé

voir file.c

ouichefs_file_get_block (modifié) → Test validé

Maximum file size achievable on a given partition :
### 1.7

voir ouichefs.h 

ouichefs_inode_info (modifié) → A Tester

ouichefs_sb_info (modifié) → A Tester

voir super.c

ouichefs_alloc_inode (modifié) → A Tester

voir fs.c

reservation_size → A Tester

voir inode.c 

ouichefs_unlink (modifié) → A Tester

voir file.c

ouichefs_garbage_collector → A Tester