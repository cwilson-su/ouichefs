/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */
#ifndef _OUICHEFS_BITMAP_H
#define _OUICHEFS_BITMAP_H

#include <linux/bitmap.h>
#include "ouichefs.h"

/*
 * Return the first free bit (set to 1) in a given in-memory bitmap spanning
 * over multiple blocks and clear it.
 * Return 0 if no free bit found (we assume that the first bit is never free
 * because of the superblock and the root inode, thus allowing us to use 0 as an
 * error value).
 */
static inline uint32_t get_first_free_bit(unsigned long *freemap,
					  unsigned long size)
{
	uint32_t ino;

	ino = find_first_bit(freemap, size);
	if (ino == size)
		return 0;

	bitmap_clear(freemap, ino, 1);

	return ino;
}

/*
 * Return an unused inode number and mark it used.
 * Return 0 if no free inode was found.
 */
static inline uint32_t get_free_inode(struct ouichefs_sb_info *sbi)
{
	uint32_t ret;

	ret = get_first_free_bit(sbi->ifree_bitmap, sbi->nr_inodes);
	if (ret) {
		sbi->nr_free_inodes--;
		pr_debug("%s:%d: allocated inode %u\n", __func__, __LINE__,
			 ret);
	}
	return ret;
}

/*
 * Return an unused block number and mark it used.
 * Return 0 if no free block was found.
 */
static inline uint32_t get_free_block(struct ouichefs_sb_info *sbi)
{
	uint32_t ret;

	ret = get_first_free_bit(sbi->bfree_bitmap, sbi->nr_blocks);
	if (ret) {
		sbi->nr_free_blocks--;
		pr_debug("%s:%d: allocated block %u\n", __func__, __LINE__,
			 ret);
	}
	return ret;
}

/*
 * Mark the i-th bit in freemap as free (i.e. 1)
 */
static inline int put_free_bit(unsigned long *freemap, unsigned long size,
			       uint32_t i)
{
	/* i is greater than freemap size */
	if (i > size)
		return -1;

	bitmap_set(freemap, i, 1);

	return 0;
}

/*
 * Mark an inode as unused.
 */
static inline void put_inode(struct ouichefs_sb_info *sbi, uint32_t ino)
{
	spin_lock(&sbi->bitmap_lock);
	if (put_free_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino))
		return;

	sbi->nr_free_inodes++;
	spin_unlock(&sbi->bitmap_lock);
	pr_debug("%s:%d: freed inode %u\n", __func__, __LINE__, ino);
}

/*
 * Mark a block as unused.
 */
static inline void put_block(struct ouichefs_sb_info *sbi, uint32_t bno)
{
	if (put_free_bit(sbi->bfree_bitmap, sbi->nr_blocks, bno))
		return;

	sbi->nr_free_blocks++;
	pr_debug("%s:%d: freed block %u\n", __func__, __LINE__, bno);
}

static inline void copy_bitmap_from_le64(unsigned long *dst, __le64 *src)
{
	int i;

	for (i = 0; i < (OUICHEFS_BLOCK_SIZE >> 3); i++) {
#if BITS_PER_LONG == 64
		dst[i] = le64_to_cpu(src[i]);
#elif BITS_PER_LONG == 32
		dst[(i << 1) + 0] = le64_to_cpu(src[i]) >> 0;
		dst[(i << 1) + 1] = le64_to_cpu(src[i]) >> 32;
#else
#error Unsupported long size.
#endif
	}
}

static inline void copy_bitmap_to_le64(__le64 *dst, unsigned long *src)
{
	int i;

	for (i = 0; i < (OUICHEFS_BLOCK_SIZE >> 3); i++) {
#if BITS_PER_LONG == 64
		dst[i] = cpu_to_le64(src[i]);
#elif BITS_PER_LONG == 32
		dst[i] = cpu_to_le64(((uint64_t)src[(i << 1) + 1] << 32) | src[i << 1]);
#else
#error Unsupported long size.
#endif
	}
}

/* ---------------------------- 1.6.1----------------------- */
/*
 * HELPER: get_contiguous_free_bits
 * Scans the bitmap to find a contiguous sequence of free blocks (1s)
 * uses a first-fit strategy: it stops as soon as it finds a run big enough 
 * to satisfy requested. If it can't find one big enough, it returns the largest 
 * available hole it could find to prevent failing entirely.
 */
static inline uint32_t get_contiguous_free_bits(unsigned long *freemap,
                                                unsigned long size,
                                                uint32_t requested,
                                                uint32_t *allocated_count)
{
    unsigned long start = 0, next_zero;
    unsigned long best_start = 0, best_len = 0;

    // Loop through the entire bitmap to find the best contiguous chunk
    while (start < size) {
        // find the next free bit (free blocks are 1s in OuicheFS)
        start = find_next_bit(freemap, size, start);
        if (start >= size)
            break; // reached the end of the filesystem

        //find where this free sequence ends (find the next 0) 
        next_zero = find_next_zero_bit(freemap, size, start);

        // Calculate how long this free sequence is
        unsigned long current_len = next_zero - start;

        // ---> if it's big enough to hold our requested data, stop immediately!
        if (current_len >= requested) {
            best_start = start;
            best_len = requested;
            break; // perfect fit, no need to search further!
        }

        // ---> If it's not big enough, check if it's the largest hole we've seen so far
        if (current_len > best_len) {
            best_start = start;
            best_len = current_len;
        }

        // Move our search pointer forward to skip over the blocks we just checked
        start = next_zero;
    }

    // if best_len is still 0, there're absolutely no free blocks left on disk
    if (best_len == 0)
        return 0;

    //mark our selected blocks as used by clearing their bits to 0 
    // imnfo: bitmap_clear is a native Linux kernel function that flips bits to 0 efficiently
    bitmap_clear(freemap, best_start, best_len);

    // Return the results via the pointer and function return
    *allocated_count = best_len;
    return best_start;
}

/*
 * ouichefs_alloc_contiguous: The main allocator called by file.c
 * Returns the number of allocated blocks, and stores the starting block number in *start_bno.
 */
static inline uint32_t ouichefs_alloc_contiguous(struct super_block *sb,
                                                 uint32_t requested,
                                                 uint32_t *start_bno)
{
    struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
    uint32_t allocated = 0;
    uint32_t bno;

    // nothing to do if 0 blocks are requested
    if (requested == 0) return 0;

	spin_lock(&sbi->bitmap_lock);
    // use helper to scan the in-memory bitmap and claim the bits
    bno = get_contiguous_free_bits(sbi->bfree_bitmap, sbi->nr_blocks, requested, &allocated);

    if (bno) {
        // update the global filesystem free block counter
        sbi->nr_free_blocks -= allocated;
        
        // chose to print a debug message to the kernel log (dmesg) so we can see it working
        pr_debug("%s:%d: allocated %u contiguous blocks starting at %u\n", __func__, __LINE__, allocated, bno);
    }

	spin_unlock(&sbi->bitmap_lock);

    // pass the physical block number back to the caller
    *start_bno = bno;
    
    // return how many blocks we successfully grabbed
    return allocated;
}

#endif /* _OUICHEFS_BITMAP_H */
