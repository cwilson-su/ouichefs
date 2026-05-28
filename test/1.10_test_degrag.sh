#!/bin/bash
echo "---> Starting OuicheFS Defrag Test (Q1.10)"

cd ..
make
gcc tets_ioctl.c -o test_extents
gcc do_defrag.c -o do_defrag

umount /mnt/ouiche 2>/dev/null
rmmod ouichefs 2>/dev/null
insmod ouichefs.ko

dd if=/dev/zero of=image.img bs=1M count=30 status=none
./mkfs/mkfs.ouichefs image.img > /dev/null
mkdir -p /mnt/ouiche
mount -o loop -t ouichefs image.img /mnt/ouiche

echo "Intentionally fragmenting a file..."
# By alternating writes between frag.bin and blocker.bin, we guarantee they get broken into separate extents!
dd if=/dev/urandom of=/mnt/ouiche/frag.bin bs=1M count=1 status=none
dd if=/dev/urandom of=/mnt/ouiche/blocker.bin bs=1M count=1 status=none
dd if=/dev/urandom of=/mnt/ouiche/frag.bin oflag=append conv=notrunc bs=1M count=1 status=none
dd if=/dev/urandom of=/mnt/ouiche/blocker.bin oflag=append conv=notrunc bs=1M count=1 status=none
dd if=/dev/urandom of=/mnt/ouiche/frag.bin oflag=append conv=notrunc bs=1M count=1 status=none

echo "=== BEFORE DEFRAG: EXTENTS ==="
./test_extents /mnt/ouiche/frag.bin

echo "=== RUNNING DEFRAG ==="
./do_defrag /mnt/ouiche/frag.bin

echo "=== AFTER DEFRAG: EXTENTS ==="
./test_extents /mnt/ouiche/frag.bin