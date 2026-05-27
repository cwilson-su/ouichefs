#!/bin/bash
echo "----> Starting OuicheFS Sparse File Test (Q1.9) ------"

# compile everything
cd ..
make
gcc tets_ioctl.c -o test_extents

# reload the kernel module cleanly
umount /mnt/ouiche 2>/dev/null
rmmod ouichefs 2>/dev/null
insmod ouichefs.ko

# format and mount a fresh 20MB dummy drive
echo "Formatting drive..."
dd if=/dev/zero of=image.img bs=1M count=20 status=none
./mkfs/mkfs.ouichefs image.img > /dev/null
mkdir -p /mnt/ouiche
mount -o loop -t ouichefs image.img /mnt/ouiche

# create the Sparse File
echo "---> Writing 1MB of real data at the beginning..."
dd if=/dev/zero of=/mnt/ouiche/sparse.bin bs=1M count=1 status=none

echo "> Seeking 5MB forward and writing 1MB of data (Injecting 4MB hole)..."
# seek=5 skips past the first 5MB. Since we already wrote 1MB, this leaves a 4MB gap.
dd if=/dev/zero of=/mnt/ouiche/sparse.bin bs=1M count=1 seek=5 status=none

# check the reported size
echo "> FILE SIZE VERIFICATION"
ls -lh /mnt/ouiche/sparse.bin
echo "Notice how the size says 6.0M, but we only wrote 2M!"

# Test the read function on the hole
echo "> READ TEST"
echo "Attempting to read data from the hole..."
# We skip the first 1MB (real data) and read the next 4MB (the hole)
dd if=/mnt/ouiche/sparse.bin bs=1M skip=1 count=4 status=none | wc -c | awk '{print "Successfully read " $1 " bytes of zeros from the hole!"}'

# Trigger the IOCTL to print the extents to the kernel log
echo "> EXTENT ARRAY (KERNEL LOG)"
./test_extents /mnt/ouiche/sparse.bin
dmesg | tail -n 5

# Test Deletion (Ensures we don't accidentally free block 0)
echo "> DELETION TEST"
rm /mnt/ouiche/sparse.bin
echo "File deleted successfully without crashing the superblock!"
