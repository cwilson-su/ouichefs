#!/bin/bash

set -e

MP=./mnt

A=$MP/fileA
B=$MP/fileB
C=$MP/fileC

rm -f "$A" "$B" "$C"

echo "======================================="
echo "TEST FS - reservation invisible model"
echo "======================================="

# utilitaire write 4K
write4k() {
    head -c 4096 /dev/zero
}

########################################
# TEST 1 : baseline extent stability
########################################

echo
echo "[TEST 1] Sequential writes on A (8 blocks)"

exec 3>>"$A"

for i in $(seq 1 8); do
    write4k >&3
done

sync

echo "[A after 8 writes]"
./test_ioctl "$A"

echo "EXPECTED:"
echo "  - 1 extent"
echo "  - ~8 blocks"

########################################
# TEST 2 : fragmentation pressure
########################################

echo
echo "[TEST 2] Fragmentation via B"

exec 4>>"$B"

for i in $(seq 1 32); do
    write4k >&4
done

sync

echo "[B state]"
./test_ioctl "$B"

echo
echo "[A continues under fragmentation]"

for i in $(seq 1 8); do
    write4k >&3
done

sync

echo "[A after fragmentation]"
./test_ioctl "$A"

echo "EXPECTED:"
echo "  - 1 or 2 extents only"
echo "  - NOT linear growth"

########################################
# TEST 3 : strong fragmentation stress
########################################

echo
echo "[TEST 3] Multi-file interleaving stress"

exec 5>>"$C"

for i in $(seq 1 64); do
    write4k >&3
    write4k >&4
    write4k >&5
done

sync

echo "[A stress result]"
./test_ioctl "$A"

echo "[B stress result]"
./test_ioctl "$B"

echo "[C stress result]"
./test_ioctl "$C"

echo "EXPECTED:"
echo "  - extents per file remain low"
echo "  - no explosion due to reservation mechanism"

########################################
# cleanup
########################################

exec 3>&-
exec 4>&-
exec 5>&-

echo
echo "======================================="
echo "TEST COMPLETED SUCCESSFULLY"
echo "======================================="