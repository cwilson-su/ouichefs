#!/bin/bash
#
# test_1_8_sysfs.sh
#
# Banc de test pour la question 1.8 — Sysfs statistics OuicheFS.
#
# Ce script teste :
#   1. L'existence de /sys/ouichefs/<partition>
#   2. La présence des fichiers sysfs demandés
#   3. La lecture de toutes les statistiques
#   4. Le caractère read-write de reservation_size
#   5. Le caractère read-only des autres fichiers
#   6. L'évolution des statistiques après création d'un petit fichier
#   7. L'évolution des statistiques après création d'un gros fichier
#   8. La libération des blocs après suppression
#   9. Une vérification de cohérence free + committed + reserved
#
# Usage dans la VM :
#   chmod +x test_1_8_sysfs.sh
#   ./test_1_8_sysfs.sh
#
# Par défaut :
#   MNT=/mnt/ouichefs
#   SYSFS_ROOT=/sys/ouichefs
#
# Si ton code crée encore /sys/kernel/ouichefs :
#   SYSFS_ROOT=/sys/kernel/ouichefs ./test_1_8_sysfs.sh
#
# Si ton point de montage est différent :
#   MNT=/mnt/ouiche ./test_1_8_sysfs.sh

set -u

MNT="${MNT:-/mnt/ouichefs}"
SYSFS_ROOT="${SYSFS_ROOT:-/sys/ouichefs}"

EXPECTED_FILES="
free_blocks
committed_blocks
reserved_blocks
files
total_extents
avg_extent_size
max_file_size
fragmentation
reservation_size
gc_runs
"

fail() {
    echo "ERROR: $1"
    exit 1
}

warn() {
    echo "WARNING: $1"
}

ok() {
    echo "OK: $1"
}

section() {
    echo
    echo "========================================"
    echo "$1"
    echo "========================================"
}

read_stat() {
    cat "$SYS/$1"
}

print_stats() {
    echo "free_blocks       = $(read_stat free_blocks)"
    echo "committed_blocks  = $(read_stat committed_blocks)"
    echo "reserved_blocks   = $(read_stat reserved_blocks)"
    echo "files             = $(read_stat files)"
    echo "total_extents     = $(read_stat total_extents)"
    echo "avg_extent_size   = $(read_stat avg_extent_size)"
    echo "max_file_size     = $(read_stat max_file_size)"
    echo "fragmentation     = $(read_stat fragmentation)"
    echo "reservation_size  = $(read_stat reservation_size)"
    echo "gc_runs           = $(read_stat gc_runs)"
}

snapshot() {
    prefix="$1"
    eval "${prefix}_free=\$(read_stat free_blocks)"
    eval "${prefix}_committed=\$(read_stat committed_blocks)"
    eval "${prefix}_reserved=\$(read_stat reserved_blocks)"
    eval "${prefix}_files=\$(read_stat files)"
    eval "${prefix}_extents=\$(read_stat total_extents)"
    eval "${prefix}_maxsize=\$(read_stat max_file_size)"
}

assert_number() {
    name="$1"
    value="$2"
    case "$value" in
        ''|*[!0-9]*)
            fail "$name should be numeric, got '$value'"
            ;;
    esac
}

section "0. Vérification du montage et de sysfs"

[ -d "$MNT" ] || fail "Mount point $MNT does not exist"
mount | grep -q " $MNT " || warn "$MNT does not appear in mount output. Continue anyway."

[ -d "$SYSFS_ROOT" ] || fail "$SYSFS_ROOT does not exist. Si tu utilises kernel_kobj, essaie : SYSFS_ROOT=/sys/kernel/ouichefs ./test_1_8_sysfs.sh"

SYS_COUNT=$(find "$SYSFS_ROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)
[ "$SYS_COUNT" -ge 1 ] || fail "No partition directory found under $SYSFS_ROOT"

SYS=$(find "$SYSFS_ROOT" -mindepth 1 -maxdepth 1 -type d | head -n 1)
echo "Sysfs partition path: $SYS"
ok "Répertoire sysfs trouvé"

section "1. Présence des fichiers attendus"

for f in $EXPECTED_FILES; do
    [ -f "$SYS/$f" ] || fail "Missing sysfs file: $SYS/$f"
    echo "found: $f"
done

ok "Tous les fichiers sysfs demandés sont présents"

section "2. Lecture initiale de toutes les statistiques"

print_stats

for f in $EXPECTED_FILES; do
    value=$(cat "$SYS/$f")
    assert_number "$f" "$value"
done

ok "Toutes les statistiques sont lisibles et numériques"

section "3. Test read-write de reservation_size"

old_reservation=$(read_stat reservation_size)
echo "Ancienne reservation_size = $old_reservation"

echo 16 > "$SYS/reservation_size" || fail "Impossible d'écrire dans reservation_size"
new_reservation=$(read_stat reservation_size)
echo "Nouvelle reservation_size = $new_reservation"

[ "$new_reservation" = "16" ] || fail "reservation_size should be 16 after write"

echo "$old_reservation" > "$SYS/reservation_size" || fail "Impossible de restaurer reservation_size"
restored_reservation=$(read_stat reservation_size)
echo "Reservation_size restaurée = $restored_reservation"

[ "$restored_reservation" = "$old_reservation" ] || fail "reservation_size was not restored"

ok "reservation_size est bien modifiable"

section "4. Test read-only des autres fichiers"

if echo 123 > "$SYS/free_blocks" 2>/dev/null; then
    fail "free_blocks should be read-only, but write succeeded"
else
    ok "free_blocks refuse bien l'écriture"
fi

if echo 123 > "$SYS/files" 2>/dev/null; then
    fail "files should be read-only, but write succeeded"
else
    ok "files refuse bien l'écriture"
fi

if echo 123 > "$SYS/gc_runs" 2>/dev/null; then
    fail "gc_runs should be read-only, but write succeeded"
else
    ok "gc_runs refuse bien l'écriture"
fi

section "5. Création d'un petit fichier"

rm -f "$MNT/test_1_8_small.txt" "$MNT/test_1_8_big.bin"

snapshot before_small

echo "hello ouichefs sysfs" > "$MNT/test_1_8_small.txt" || fail "Impossible d'écrire le petit fichier"
sync

snapshot after_small

echo "Avant : files=$before_small_files committed=$before_small_committed extents=$before_small_extents free=$before_small_free max_size=$before_small_maxsize"
echo "Après : files=$after_small_files committed=$after_small_committed extents=$after_small_extents free=$after_small_free max_size=$after_small_maxsize"

if [ "$after_small_files" -le "$before_small_files" ]; then
    fail "files should increase after creating a file"
fi

if [ "$after_small_committed" -le "$before_small_committed" ]; then
    fail "committed_blocks should increase after writing a file"
fi

if [ "$after_small_extents" -le "$before_small_extents" ]; then
    fail "total_extents should increase after writing a file"
fi

ok "Les stats évoluent correctement après création d'un petit fichier"

section "6. Création d'un gros fichier"

snapshot before_big

dd if=/dev/zero of="$MNT/test_1_8_big.bin" bs=4K count=20 >/dev/null 2>&1 || fail "Impossible d'écrire le gros fichier"
sync

snapshot after_big

echo "Avant : files=$before_big_files committed=$before_big_committed extents=$before_big_extents free=$before_big_free max_size=$before_big_maxsize"
echo "Après : files=$after_big_files committed=$after_big_committed extents=$after_big_extents free=$after_big_free max_size=$after_big_maxsize"
echo "avg_extent_size=$(read_stat avg_extent_size)"
echo "fragmentation=$(read_stat fragmentation)"

if [ "$after_big_files" -le "$before_big_files" ]; then
    fail "files should increase after creating big file"
fi

if [ "$after_big_committed" -le "$before_big_committed" ]; then
    fail "committed_blocks should increase after writing big file"
fi

if [ "$after_big_maxsize" -lt 81920 ]; then
    fail "max_file_size should be at least 81920 bytes after writing 20 blocks"
fi

ok "Les stats évoluent correctement après création d'un gros fichier"

section "7. Suppression du gros fichier"

snapshot before_rm

rm -f "$MNT/test_1_8_big.bin" || fail "Impossible de supprimer le gros fichier"
sync

snapshot after_rm

echo "Avant rm : files=$before_rm_files committed=$before_rm_committed free=$before_rm_free"
echo "Après rm : files=$after_rm_files committed=$after_rm_committed free=$after_rm_free"

if [ "$after_rm_files" -ge "$before_rm_files" ]; then
    fail "files should decrease after removing big file"
fi

if [ "$after_rm_committed" -ge "$before_rm_committed" ]; then
    fail "committed_blocks should decrease after removing big file"
fi

if [ "$after_rm_free" -le "$before_rm_free" ]; then
    fail "free_blocks should increase after removing big file"
fi

ok "Les stats diminuent correctement après suppression"

section "8. Vérification de cohérence free + committed + reserved"

FREE=$(read_stat free_blocks)
COMM=$(read_stat committed_blocks)
RES=$(read_stat reserved_blocks)
SUM=$((FREE + COMM + RES))

echo "free=$FREE committed=$COMM reserved=$RES"
echo "sum=$SUM"

assert_number "free_blocks" "$FREE"
assert_number "committed_blocks" "$COMM"
assert_number "reserved_blocks" "$RES"

if [ "$SUM" -le 0 ]; then
    fail "free + committed + reserved should be positive"
fi

ok "La somme free + committed + reserved est cohérente numériquement"

section "9. Nettoyage"

rm -f "$MNT/test_1_8_small.txt" "$MNT/test_1_8_big.bin"
sync

print_stats

ok "Banc de test 1.8 terminé avec succès"
