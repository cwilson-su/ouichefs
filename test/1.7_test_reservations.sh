#!/bin/bash
echo "---OUICHEFS STEP 1.7 TEST (RESERVATIONS & GC)---"

# 1. Setup & Mount
cd ..
make > /dev/null 2>&1

umount /mnt/ouiche 2>/dev/null
rmmod ouichefs 2>/dev/null

# Insert the module and force a massive reservation size (64 blocks = 256 KB)
# This makes it very easy to see the blocks being held hostage!
insmod ouichefs.ko reservation_size=64

# Format a TINY 2MB drive so we can fill it up easily
echo "Formatting tiny 2MB drive..."
dd if=/dev/zero of=image.img bs=1M count=2 status=none
./mkfs/mkfs.ouichefs image.img > /dev/null
mkdir -p /mnt/ouiche
mount -o loop -t ouichefs image.img /mnt/ouiche

echo ""
echo ">>> TEST 1: The Release Hook (Step 1.7.3) <<<"
echo "Writing 1 block (4KB) to release_test.bin..."
dd if=/dev/zero of=/mnt/ouiche/release_test.bin bs=4K count=1 status=none

echo "Because 'dd' instantly closed the file, your release hook should have"
echo "returned the remaining 63 reserved blocks to the disk."
echo "Current disk usage (Should be barely anything):"
du -h /mnt/ouiche/release_test.bin

echo ""
echo ">>> TEST 2: The Garbage Collector (Step 1.7.4) <<<"
echo "1. Opening a file in the background to hold blocks hostage..."
# TRICK: Open file descriptor 3 pointing to hostage.bin
exec 3> /mnt/ouiche/hostage.bin

# Write exactly 1 byte. The filesystem will allocate 1 block, and reserve 63!
echo "x" >&3
echo "   hostage.bin is now secretly holding 63 blocks in RAM!"

echo "2. Filling up the rest of the physical disk..."
# We write zeros until the disk throws an "ENOSPC" (No space left on device) error.
dd if=/dev/zero of=/mnt/ouiche/filler.bin bs=4K status=none 2>/dev/null
echo "   Disk is now completely 100% full!"

echo "3. Attempting to write a brand new file..."
echo "(Since the disk is full, this MUST trigger the Garbage Collector to"
echo "steal the 63 blocks back from hostage.bin!)"

# The moment of truth:
echo "GC Trigger Text" > /mnt/ouiche/trigger.txt 2>/dev/null

if [ $? -eq 0 ]; then
    echo "   --> SUCCESS! The write completed. The GC fired and reclaimed space!"
else
    echo "   --> FAILURE! The write crashed. The GC failed to reclaim the reserved blocks."
fi

# Close the held file descriptor to clean up
exec 3>&-

echo ""
echo "#################################################################################"
echo "-----------KERNEL LOGS (Look for your GC trigger print)-------"
dmesg | tail -n 6
