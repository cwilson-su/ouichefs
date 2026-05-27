#!/bin/bash
echo "----- Lets test OuicheFS Allocator for Q1.6.2 ----"

# ensure we are in the right directory and everything is built
make
gcc tets_ioctl.c -o test_extents

# reload the kernel module cleanly
umount /mnt/ouiche 2>/dev/null
rmmod ouichefs 2>/dev/null
insmod ouichefs.ko

# format & mount a fresh 20MB dummy drive
echo "Formatting drive..."
dd if=/dev/zero of=image.img bs=1M count=20 status=none
./mkfs/mkfs.ouichefs image.img > /dev/null
mkdir -p /mnt/ouiche
mount -o loop -t ouichefs image.img /mnt/ouiche

# create a massive 10MB file
echo "Writing 10MB file to disk..."
dd if=/dev/zero of=/mnt/ouiche/massive.bin bs=1M count=10

# trigger the IOCTL to print the extents to the kernel log
echo "Querying extents..."
./test_extents /mnt/ouiche/massive.bin

# read the last few lines of the kernel log as usual
echo "-------> KERNEL LOG OUTPUT "
dmesg | tail -n 5
