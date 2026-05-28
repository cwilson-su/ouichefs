// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include "ouichefs.h"
#include "bitmap.h"

/* 1.4.1 Logical-to-physical block translation */
static uint32_t ouichefs_extent_get_block(
	struct ouichefs_extent *extents, uint32_t logical_block, sector_t* extent_id)
{
	sector_t iblock;
	uint32_t physical_block = 0;
	uint32_t block = 0;

	for (iblock = 0; iblock < OUICHEFS_MAX_EXTENTS; iblock++) {
		uint32_t start = extents[iblock].start;
		uint32_t count = extents[iblock].count;

		if (count == 0) {
			if (extent_id)
				*extent_id = iblock;
			break;
		}
			

		if (logical_block >= block && logical_block < block + count) {
			if (extent_id)
				*extent_id = iblock;

			if (start == 0) 
				break;

			physical_block = start + logical_block - block;
			break;
		}
		
		block += count;
	}

	if (iblock == OUICHEFS_MAX_EXTENTS) 
		if (extent_id) 
			*extent_id = OUICHEFS_MAX_EXTENTS; // logical_block est en dehors de la table des extents
	
	return physical_block;
}

/* 1.6.1 Implementation */
/*static uint32_t ouichefs_alloc_contiguous(struct super_block *sb, uint32_t requested, uint32_t *block)
{
	struct ouichefs_sb_info* sbi = OUICHEFS_SB(sb);
	uint32_t total_blocks = sbi->nr_blocks;
    uint32_t best_start = 0;
    uint32_t best_count = 0;
    uint32_t current_start = 0;
    uint32_t current_count = 0;
	uint32_t first_free_bit;
    bool in_free_run = false;

	if (requested == 0)
		return 0;

	first_free_bit = find_first_bit(sbi->bfree_bitmap, total_blocks);

	// Si aucun block libre on renvoie 0
	if (first_free_bit == total_blocks)
		return 0;

	for (uint32_t b = first_free_bit; b < total_blocks; b++) {

		bool is_free = test_bit(b, sbi->bfree_bitmap);
		
		if (is_free) {
			if (!in_free_run) {
				current_start = b;
				current_count = 1;
				in_free_run = true;
			} else {
				if (current_count++ == requested) {
					best_start = current_start;
					best_count = current_count;
					goto allocation;
				}
			}
		} else {
			if (in_free_run) {
				in_free_run = false;

				if (current_count > best_count) {
					best_start = current_start;
					best_count = current_count;
				}
			}
		}
	}

allocation:
	if (best_count > 0) {
		*block = best_start;

		for (uint32_t b = best_start; b < best_start + best_count; b++) 
			bitmap_clear(sbi->bfree_bitmap, b, 1);
		
		sbi->nr_free_blocks -= best_count;
	}

	return best_count;
}*/

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true, allocate a new block on disk and map it.
 */
//static int ouichefs_file_get_block(struct inode *inode, sector_t iblock,
//				   struct buffer_head *bh_result, int create)
//{
//	struct super_block *sb = inode->i_sb;
//	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
//	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
//	struct ouichefs_file_index_block *index;
//	struct buffer_head *bh_index;
//	int ret = 0, bno;
//
//	/* If block number exceeds filesize, fail */
//	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
//		return -EFBIG;
//
//	/* Read index block from disk */
//	bh_index = sb_bread(sb, ci->index_block);
//	if (!bh_index)
//		return -EIO;
//	index = (struct ouichefs_file_index_block *)bh_index->b_data;
//
//	/*
//	 * Check if iblock is already allocated. If not and create is true,
//	 * allocate it. Else, get the physical block number.
//	 */
//
//	//if (index->blocks[iblock] == 0) {
//	if (index->blocks[iblock].count == 0) {
//		if (!create) {
//			ret = 0;
//			goto brelse_index;
//		}
//		bno = get_free_block(sbi);
//		if (!bno) {
//			ret = -ENOSPC;
//			goto brelse_index;
//		}
//		//index->blocks[iblock] = cpu_to_le32(bno);
//		index->blocks[iblock].start = bno;
//		index->blocks[iblock].count = 1;
//		mark_buffer_dirty(bh_index);
//	} else {
//		//bno = le32_to_cpu(index->blocks[iblock]);
//		bno = ouichefs_extent_get_block(index->blocks, iblock);
//	}
//
//	/* Map the physical block to the given buffer_head */
//	map_bh(bh_result, sb, bno);
//
//brelse_index:
//	brelse(bh_index);
//
//	return ret;
//}

/* 1.5.1 Extent-aware block allocation */
//static int ouichefs_file_get_block(struct inode *inode, sector_t logical_block,
//				   struct buffer_head *bh_result, int create)
//{
//	struct super_block *sb = inode->i_sb;
//	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
//	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
//	struct ouichefs_file_index_block *index;
//	struct buffer_head *bh_index;
//	int ret = 0, bno;
//	sector_t current_extent_id;
//	sector_t last_extent_id = 0;
//
//	/* Read index block from disk */
//	bh_index = sb_bread(sb, ci->index_block);
//	if (!bh_index)
//		return -EIO;
//	index = (struct ouichefs_file_index_block *)bh_index->b_data;
//
//	/*
//	 * Check if logical_block is already allocated. If not and create is true,
//	 * allocate it. Else, get the physical block number.
//	 */
//
//	bno = ouichefs_extent_get_block(index->blocks, logical_block, &current_extent_id);
//
//	if (bno == 0) {
//		if (!create) {
//			ret = 0;
//			goto brelse_index;
//		}
//		bno = get_free_block(sbi);
//		if (!bno) {
//			ret = -ENOSPC;
//			goto brelse_index;
//		}
//
//		if (current_extent_id > 0)
//			last_extent_id = current_extent_id - 1;
//
//		// Si le bloc récupéré est contigue au dernier extents count + 1
//		if (bno == index->blocks[last_extent_id].start + index->blocks[last_extent_id].count) {
//			index->blocks[last_extent_id].count += 1;
//		} else {
//			if (current_extent_id == OUICHEFS_MAX_EXTENTS) {
//				ret = -ENOSPC;
//				goto brelse_index;
//			}
//
//			// Si on est dans un trou
//			/*if (index->blocks[current_extent_id].count != 0) {
//		
//
//			} else {
//				index->blocks[current_extent_id].start = bno;
//				index->blocks[current_extent_id].count = 1;
//			}*/
//			// !!! On ne gère pas encore les trous
//			index->blocks[current_extent_id].start = bno;
//			index->blocks[current_extent_id].count = 1;
//		}
//		
//		mark_buffer_dirty(bh_index);
//	} 
//
//	/* Map the physical block to the given buffer_head */
//	map_bh(bh_result, sb, bno);
//
//brelse_index:
//	brelse(bh_index);
//
//	return ret;
//}

/* 1.7.4 Garbage collector */
static void ouichefs_garbage_collector(struct super_block* sb)
{
	struct ouichefs_sb_info* sbi = OUICHEFS_SB(sb);
	struct inode* inode;

	sbi->gc_runs++;

	spin_lock(&sb->s_inode_list_lock);

	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		
		struct ouichefs_inode_info* ci = OUICHEFS_INODE(inode);

		if (spin_trylock(&inode->i_lock)) {
			uint32_t count = ci->i_reserved_count;
			uint32_t start = ci->i_reserved_start;

			// On remet immédiatement à zéro en mémoire sous verrou
			ci->i_reserved_start = 0;
			ci->i_reserved_count = 0;

			spin_unlock(&inode->i_lock);

			/* * Maintenant qu'on a relâché le verrou de l'inode et qu'on possède
				* les variables locales, on peut libérer les blocs dans la bitmap.
				* Note : Si put_block utilise un mutex interne, il est préférable
				* de relâcher aussi s_inode_list_lock, mais pour un petit FS de TP,
				* purger directement ici est souvent toléré si put_block est ultra-rapide.
				*/
			for (uint32_t i = 0; i < count; i++) {
				put_block(sbi, start + i);
			}
		}
	}

	spin_unlock(&sb->s_inode_list_lock);
	pr_info("ouichefs: GC pass completed (Total runs: %u)\n", sbi->gc_runs);
}

/* 1.6.2 Integration */
static int ouichefs_file_get_block(struct inode *inode, sector_t logical_block,
				   struct buffer_head *bh_result, int create, uint32_t* requested)
{
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	int ret = 0;
	uint32_t bno;
	uint32_t allocated_block;
	uint32_t to_request = reservation_size;
	sector_t current_extent_id;
	sector_t last_extent_id = 0;
	bool in_window = true;
	uint32_t new_window;

	if (*requested == 0) 
		return 0;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/*
	 * Check if logical_block is already allocated. If not and create is true,
	 * allocate it. Else, get the physical block number.
	 */

	bno = ouichefs_extent_get_block(index->blocks, logical_block, &current_extent_id);

	if (bno == 0) {
		if (!create) {
			ret = 0;
			goto brelse_index;
		}

		if (current_extent_id > 0)
			last_extent_id = current_extent_id - 1;

		if (ci->i_reserved_count) {
			if (*requested > ci->i_reserved_count) {
				bno = ci->i_reserved_start;
				index->blocks[last_extent_id].count += ci->i_reserved_count;
				*requested -= ci->i_reserved_count;
				ci->i_reserved_start += ci->i_reserved_count;
				ci->i_reserved_count = 0;	
			} else {
				index->blocks[last_extent_id].count += *requested;
				ci->i_reserved_start += *requested;
				ci->i_reserved_count -= *requested;
				*requested = 0;
			}

			goto dirty_index;
		}

		if (to_request < *requested) {
			in_window = false;
			to_request = *requested;
		}
			
		allocated_block = ouichefs_alloc_contiguous(sb, to_request, &bno);
		if (!allocated_block) {
			ouichefs_garbage_collector(sb);
			allocated_block = ouichefs_alloc_contiguous(sb, to_request, &bno);
			if (!allocated_block) {
				ret = -ENOSPC;
				goto brelse_index;
			}
		}

		if (allocated_block < *requested) {
			new_window = 0;
			*requested -= allocated_block;
		} else { 
			new_window = allocated_block - *requested;
			*requested = 0;
		}

		ci->i_reserved_start = bno + allocated_block - new_window;
		ci->i_reserved_count = new_window;

		// Si le bloc récupéré est contigue au dernier extents count + 1
		if (bno == index->blocks[last_extent_id].start + index->blocks[last_extent_id].count) {
			index->blocks[last_extent_id].count += allocated_block - new_window;
		} else {
			if (current_extent_id == OUICHEFS_MAX_EXTENTS) {
				ret = -ENOSPC;
				goto brelse_index;
			}

			// Si on est dans un trou
			/*if (index->blocks[current_extent_id].count != 0) {
		

			} else {
				index->blocks[current_extent_id].start = bno;
				index->blocks[current_extent_id].count = 1;
			}*/
			// !!! On ne gère pas encore les trous
			index->blocks[current_extent_id].start = bno;
			index->blocks[current_extent_id].count = allocated_block - new_window;
		}
dirty_index:
		mark_buffer_dirty(bh_index);
	} 

	/* Map the physical block to the given buffer_head */
	map_bh(bh_result, sb, bno);

brelse_index:
	brelse(bh_index);

	return ret;
}

/*
 * Called by the page cache to read a page from the physical disk and map it in
 * memory.
 */
//static void ouichefs_readahead(struct readahead_control *rac)
//{
//	mpage_readahead(rac, ouichefs_file_get_block);
//}

/*
 * Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
/*static int ouichefs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ouichefs_file_get_block, wbc);
}*/

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
//static int ouichefs_write_begin(struct file *file,
//				struct address_space *mapping, loff_t pos,
//				unsigned int len, struct page **pagep,
//				void **fsdata)
//{
//	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
//	int err;
//	uint32_t nr_allocs = 0;
//
//	/* Check if the write can be completed (enough space?) */
//	if (pos + len > OUICHEFS_MAX_FILESIZE)
//		return -ENOSPC;
//	nr_allocs = max(pos + len, file->f_inode->i_size) / OUICHEFS_BLOCK_SIZE;
//	if (nr_allocs > file->f_inode->i_blocks - 1)
//		nr_allocs -= file->f_inode->i_blocks - 1;
//	else
//		nr_allocs = 0;
//	if (nr_allocs > sbi->nr_free_blocks)
//		return -ENOSPC;
//
//	/* prepare the write */
//	err = block_write_begin(mapping, pos, len, pagep,
//				ouichefs_file_get_block);
//	/* if this failed, reclaim newly allocated blocks */
//	if (err < 0) {
//		pr_err("%s:%d: newly allocated blocks reclaim not implemented yet\n",
//		       __func__, __LINE__);
//	}
//	return err;
//}
//
///*
// * Called by the VFS after writing data from a write() syscall to the page
// * cache. This functions updates inode metadata and truncates the file if
// * necessary.
// */
//static int ouichefs_write_end(struct file *file, struct address_space *mapping,
//			      loff_t pos, unsigned int len, unsigned int copied,
//			      struct page *page, void *fsdata)
//{
//	int ret;
//	struct inode *inode = file->f_inode;
//	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
//	struct super_block *sb = inode->i_sb;
//	
//	/* Complete the write() */
//	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
//	if (ret < len) {
//		pr_err("%s:%d: wrote less than asked... what do I do? nothing for now...\n",
//		       __func__, __LINE__);
//	} else {
//		uint32_t nr_blocks_old = inode->i_blocks;
//
//		/* Update inode metadata */
//		inode->i_blocks = (roundup(inode->i_size, OUICHEFS_BLOCK_SIZE) /
//				   OUICHEFS_BLOCK_SIZE) +
//				  1;
//		inode->i_mtime = inode->i_ctime = current_time(inode);
//		mark_inode_dirty(inode);
//
//		/* If file is smaller than before, free unused blocks */
//		if (nr_blocks_old > inode->i_blocks) {
//			int i;
//			struct buffer_head *bh_index;
//			struct ouichefs_file_index_block *index;
//
//			/* Free unused blocks from page cache */
//			truncate_pagecache(inode, inode->i_size);
//
//			/* Read index block to remove unused blocks */
//			bh_index = sb_bread(sb, ci->index_block);
//			if (!bh_index) {
//				pr_err("failed truncating '%s'. we just lost %llu blocks\n",
//				       file->f_path.dentry->d_name.name,
//				       nr_blocks_old - inode->i_blocks);
//				goto end;
//			}
//			index = (struct ouichefs_file_index_block *)
//					bh_index->b_data;
//
//			for (i = inode->i_blocks - 1; i < nr_blocks_old - 1;
//			     i++) {
//				//put_block(OUICHEFS_SB(sb), le32_to_cpu(index->blocks[i]));
//				//index->blocks[i] = 0;
//				put_block(OUICHEFS_SB(sb), index->blocks[i].start);
//				index->blocks[i] = (struct ouichefs_extent){0, 0};
//			}
//			mark_buffer_dirty(bh_index);
//			brelse(bh_index);
//		}
//	}
//end:
//	return ret;
//}

/*const struct address_space_operations ouichefs_aops = {
	.readahead = ouichefs_readahead,
	.writepage = ouichefs_writepage,
	.write_begin = ouichefs_write_begin,
	.write_end = ouichefs_write_end
};*/

static int ouichefs_open(struct inode *inode, struct file *file)
{
	bool wronly = (file->f_flags & O_WRONLY) != 0;
	bool rdwr = (file->f_flags & O_RDWR) != 0;
	bool trunc = (file->f_flags & O_TRUNC) != 0;

	if ((wronly || rdwr) && trunc && (inode->i_size != 0)) {
		struct super_block *sb = inode->i_sb;
		struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct ouichefs_file_index_block *index;
		struct buffer_head *bh_index;
		sector_t iblock;

		/* Read index block from disk */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		/*
		for (iblock = 0; index->blocks[iblock].count != 0; iblock++) {
			//put_block(sbi, le32_to_cpu(index->blocks[iblock]));
			//index->blocks[iblock] = 0;
			put_block(sbi, index->blocks[iblock].start);
			index->blocks[iblock] = (struct ouichefs_extent){0, 0};
		}
		*/		

		for (iblock = 0; index->blocks[iblock].count != 0; iblock++) {
			// Q1.9 FIX: Do not free physical block 0 (it's a hole!) 
			if (index->blocks[iblock].start != 0) {
				put_block(sbi, index->blocks[iblock].start);
			}
			index->blocks[iblock] = (struct ouichefs_extent){0, 0};
		}

		inode->i_size = 0;
		inode->i_blocks = 1;

		mark_buffer_dirty(bh_index);
		brelse(bh_index);
	}

	return 0;
}


/* 1.2 Reimplementation of the read and the write functions */
/*ssize_t ouichefs_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
	struct inode* inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index, *bh_data;
	sector_t iblock;
	u32 block;
	ssize_t bytes_copied = 0;
	int read_error = 0;
	size_t to_read = len;
	loff_t off = *offset;

	if (off >= inode->i_size)
        return 0; // Fin de fichier (EOF)

	if (off + len > inode->i_size)
		to_read = inode->i_size - off;

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		pr_err("Failed to read inode block %d\n", ci->index_block);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	// On calcul l'indice du premier bloc à lire
	iblock = off / OUICHEFS_BLOCK_SIZE;
	off = off % OUICHEFS_BLOCK_SIZE;

	// On lit tant qu'on a des données à lire et qu'on a pas atteint le dernier block
	for (; to_read > 0; iblock++) {

		size_t to_copy = OUICHEFS_BLOCK_SIZE - off;

		if (to_read < to_copy)
			to_copy = to_read;

		// Gestion des "trous"
		if (index->blocks[iblock] == 0) { 
			size_t uncopied = clear_user(buf + bytes_copied, to_copy);
			
			to_read -= (to_copy - uncopied);
			bytes_copied += (to_copy - uncopied);
			off = 0;

			if (uncopied) {
				read_error = -EFAULT;
				break;
			}

			continue;
		}

		block = le32_to_cpu(index->blocks[iblock]);

		bh_data = sb_bread(sb, block);
		
		if (!bh_data) {
			pr_err("Failed to read data block %u\n", block);
			read_error = -EIO;
			break;
		}

		size_t ret = (size_t)copy_to_user((buf + bytes_copied), (bh_data->b_data + off), to_copy);

		to_read -= (to_copy - ret);
		bytes_copied += (to_copy - ret);
		off = 0;

		brelse(bh_data);

		if (ret) {
			read_error = -EFAULT;
			break;
		}
	}

	brelse(bh_index);

	if (bytes_copied == 0 && read_error)
		return read_error;

	*offset += bytes_copied;

	return bytes_copied;
}*/

/*ssize_t ouichefs_write(struct file* file, const char __user* buf, size_t len, loff_t* offset) {
	struct inode* inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index, *bh_data;
	sector_t iblock;
	u32 block;
	ssize_t bytes_copied = 0;
	int write_error = 0;
	size_t to_write = len;
	loff_t off;
	uint32_t nr_allocs = 0;

	// Protection obligatoire contre les accès concurrents sur le même inode
    inode_lock(inode);

	if (file->f_flags & O_APPEND)
		*offset = inode->i_size;

	off = *offset;

	// Check if the write can be completed (enough space?) 
	if (off + len > OUICHEFS_MAX_FILESIZE) {
		inode_unlock(inode);
		return -ENOSPC;
	}

	nr_allocs = max((loff_t)(off + len),(loff_t)inode->i_size) / OUICHEFS_BLOCK_SIZE;

	if (nr_allocs > inode->i_blocks - 1)
		nr_allocs -= inode->i_blocks - 1;
	else
		nr_allocs = 0;

	if (nr_allocs > sbi->nr_free_blocks) {
		inode_unlock(inode);
		return -ENOSPC;
	}

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		pr_err("Failed to read inode block %d\n", ci->index_block);
		inode_unlock(inode);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	// On calcul l'indice du premier bloc à écrire
	iblock = off / OUICHEFS_BLOCK_SIZE;
	off = off % OUICHEFS_BLOCK_SIZE;

	for (; to_write > 0; iblock++) {
		size_t to_copy = OUICHEFS_BLOCK_SIZE - off;

		if (to_write < to_copy)
			to_copy = to_write;

		//block = le32_to_cpu(index->blocks[iblock]);
		block = index->blocks[iblock].start;

		if (block == 0) {
			struct buffer_head bh_tmp = {0};
			write_error = ouichefs_file_get_block(inode, iblock, &bh_tmp, 1); // On alloue le bloc
			if (write_error){
				pr_err("Failed to allocate new data block\n");
				break;
			}
				
			block = bh_tmp.b_blocknr;

			bh_data = sb_getblk(sb, block);

			if (!bh_data) {
				pr_err("Failed to get data block %u\n", block);
				write_error = -EIO;
				break;
			}

			// Nettoyage du buffer récupéré
			memset(bh_data->b_data, 0, OUICHEFS_BLOCK_SIZE);
			set_buffer_uptodate(bh_data);
		} else {
			bh_data = sb_bread(sb, block);

			if (!bh_data) {
				pr_err("Failed to get data block %u\n", block);
				write_error = -EIO;
				break;
			}
		}

		size_t ret = (size_t)copy_from_user((bh_data->b_data + off), (buf + bytes_copied), to_copy);

		to_write -= (to_copy - ret);
		bytes_copied += (to_copy - ret);
		off = 0;

		mark_buffer_dirty(bh_data);
		brelse(bh_data);

		if (ret) {
			write_error = -EFAULT;
			break;
		}
	}

	brelse(bh_index);

	if (bytes_copied == 0 && write_error) {
		inode_unlock(inode);
		return write_error;
	}
		

	*offset += bytes_copied;

	// Update inode metadata 
	inode->i_size = (*offset > inode->i_size) ? *offset : inode->i_size;
	inode->i_blocks = (roundup(inode->i_size, OUICHEFS_BLOCK_SIZE) / OUICHEFS_BLOCK_SIZE) + 1;
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

	inode_unlock(inode);

	//pr_info("ouichefs_write : copied %ld bytes", bytes_copied);
	return bytes_copied;
}*/


/* 1.3.2 Debugging ioctl */
#define OUICHEFS_IOC_GET_EXTENTS _IO('O', 1)

#define OUICHEFS_IOC_DEFRAG_FILE _IO('O', 2) //Q1.10

/* HELPER 1: Handles the block-by-block data copying and frees the old extents */
static void ouichefs_copy_defrag_data(struct super_block *sb, 
                                      struct ouichefs_file_index_block *index, 
                                      uint32_t new_start) 
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	uint32_t logical_block = 0;
	int i;

	for (i = 0; i < OUICHEFS_MAX_EXTENTS; i++) {
		uint32_t old_start = index->blocks[i].start;
		uint32_t old_count = index->blocks[i].count;
		uint32_t j;

		if (old_count == 0) break;

		for (j = 0; j < old_count; j++) {
			// Get the brand new block from memory
			struct buffer_head *bh_new = sb_getblk(sb, new_start + logical_block);
			
			if (old_start == 0) {
				// Q1.9 INTEGRATION: It's a hole! Fill the new block with zeros
				memset(bh_new->b_data, 0, OUICHEFS_BLOCK_SIZE);
			} else {
				// Real data: Read the old block from disk and copy it over
				struct buffer_head *bh_old = sb_bread(sb, old_start + j);
				if (bh_old) {
					memcpy(bh_new->b_data, bh_old->b_data, OUICHEFS_BLOCK_SIZE);
					brelse(bh_old);
				} else {
					memset(bh_new->b_data, 0, OUICHEFS_BLOCK_SIZE);
				}
				// Return the old block to the free bitmap
				put_block(sbi, old_start + j);
			}

			// Tell the kernel to save the new block 
			set_buffer_uptodate(bh_new);
			mark_buffer_dirty(bh_new);
			sync_dirty_buffer(bh_new);
			brelse(bh_new);

			logical_block++;
		}
		// Erase the old extent from the array
		index->blocks[i] = (struct ouichefs_extent){0, 0};
	}
}

/* HELPER 2: Orchestrates the defragmentation process for a file */
static long ouichefs_do_defrag(struct file *file)
{
	struct inode* inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	uint32_t total_blocks, new_start = 0, allocated;
	int i;

	inode_lock(inode);

	// calculate how many blocks the file needs in total
	total_blocks = (inode->i_size + OUICHEFS_BLOCK_SIZE - 1) / OUICHEFS_BLOCK_SIZE;
	if (total_blocks <= 1) {
		inode_unlock(inode);
		return 0; /* Nothing to defragment! */
	}

	// Read the index block
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index) {
		inode_unlock(inode);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	// Check if it is ALREADY defragmented
	if (index->blocks[1].count == 0 && index->blocks[0].start != 0) {
		brelse(bh_index);
		inode_unlock(inode);
		return 0;
	}

	// Try to allocate a single massive contiguous chunk 
	allocated = ouichefs_alloc_contiguous(sb, total_blocks, &new_start);
	if (allocated < total_blocks) {
		// Not enough contiguous space on the disk! Abort cleanly.
		for (i = 0; i < allocated; i++) {
			put_block(OUICHEFS_SB(sb), new_start + i);
		}
		brelse(bh_index);
		inode_unlock(inode);
		pr_err("ouichefs: Cannot defrag, disk is too fragmented!\n");
		return -ENOSPC;
	}

	// Call our copy helper (HELPER1) to do the heavy lifting!
	ouichefs_copy_defrag_data(sb, index, new_start);

	// Write the glorious single extent at the top of the array!
	index->blocks[0].start = new_start;
	index->blocks[0].count = total_blocks;

	mark_buffer_dirty(bh_index);
	sync_dirty_buffer(bh_index);
	brelse(bh_index);
	
	inode->i_mtime = inode_get_ctime(inode) = current_time(inode);
	mark_inode_dirty(inode);

	inode_unlock(inode);
	pr_info("ouichefs: Defragmented inode %lu into %u blocks at physical block %u\n", inode->i_ino, total_blocks, new_start);
	
	return 0;
}

long ouichefs_ioctl(struct file* file, unsigned int cmd, unsigned long arg) {
	switch (cmd) {
		case OUICHEFS_IOC_GET_EXTENTS :
			{
				struct inode* inode = file->f_inode;
				struct super_block *sb = inode->i_sb;
				struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
				struct ouichefs_file_index_block *index;
				struct buffer_head *bh_index;
				sector_t iblock;
				int nb_extents = 0;

				// Read index block from disk 
				bh_index = sb_bread(sb, ci->index_block);
				if (!bh_index){
					pr_err("Failed to read inode block %d\n", ci->index_block);
					return -EIO;
				}
				index = (struct ouichefs_file_index_block *)bh_index->b_data;			
				
				for (int i = 0; index->blocks[i].count != 0; i++) 
					nb_extents++;

				pr_info("ouichefs: extents for inode %lu: %d extent(s)\n", inode->i_ino, nb_extents);

				for (iblock = 0; index->blocks[iblock].count != 0; iblock++) {
					struct ouichefs_extent ext = index->blocks[iblock];

					if (ext.start == 0)
						pr_info("	[%llu] HOLE count=%u\n", iblock, ext.count);
					else
						pr_info("	[%llu] start=%u count=%u (blocks %u-%u)\n", iblock, ext.start, ext.count, ext.start, ext.start + ext.count - 1);
				}

				brelse(bh_index);

				break;
			}
			case OUICHEFS_IOC_DEFRAG_FILE:
				return ouichefs_do_defrag(file);
		default :
			return -ENOTTY;
	}

	return 0;
}

/* 1.4 Updating the read function*/
ssize_t ouichefs_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
	struct inode* inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index, *bh_data;
	sector_t iblock;
	u32 block;
	ssize_t bytes_copied = 0;
	int read_error = 0;
	size_t to_read = len;
	loff_t off = *offset;

	if (off >= inode->i_size)
        return 0; // Fin de fichier (EOF)

	if (off + len > inode->i_size)
		to_read = inode->i_size - off;

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		pr_err("Failed to read inode block %d\n", ci->index_block);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;
	struct ouichefs_extent* extents = index->blocks;

	// On calcul l'indice du premier bloc à lire
	iblock = off / OUICHEFS_BLOCK_SIZE;
	off = off % OUICHEFS_BLOCK_SIZE;

	// On lit tant qu'on a des données à lire et qu'on a pas atteint le dernier block
	for (; to_read > 0; iblock++) {
		size_t to_copy = OUICHEFS_BLOCK_SIZE - off;

		if (to_read < to_copy)
			to_copy = to_read;

		block = ouichefs_extent_get_block(extents, iblock, NULL);

		// Gestion des "trous"
		if (block == 0) {
			size_t uncopied = clear_user(buf + bytes_copied, to_copy);
				
			to_read -= (to_copy - uncopied);
			bytes_copied += (to_copy - uncopied);
			off = 0;

			if (uncopied) {
				read_error = -EFAULT;
				break;
			}

			continue;
		}

		bh_data = sb_bread(sb, block);

		if (!bh_data) {
			pr_err("Failed to read data block %u\n", block);
			read_error = -EIO;
			break;
		}

		size_t ret = (size_t)copy_to_user((buf + bytes_copied), (bh_data->b_data + off), to_copy);

		to_read -= (to_copy - ret);
		bytes_copied += (to_copy - ret);
		off = 0;

		brelse(bh_data);

		if (ret) {
			read_error = -EFAULT;
			break;
		}
	}

	brelse(bh_index);

	if (bytes_copied == 0 && read_error)
		return read_error;

	*offset += bytes_copied;
	
	return bytes_copied;
}

/* 1.5 Updating write fonction */
ssize_t ouichefs_write(struct file* file, const char __user* buf, size_t len, loff_t* offset) {
	struct inode* inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index, *bh_data;
	sector_t iblock;
	u32 block;
	ssize_t bytes_copied = 0;
	int write_error = 0;
	size_t to_write = len;
	loff_t off;
	uint32_t nr_allocs = 0;

	// Protection obligatoire contre les accès concurrents sur le même inode
    inode_lock(inode);

	if (file->f_flags & O_APPEND)
		*offset = inode->i_size;

	off = *offset;

	// On vérifie qu'il y a assez de blocks libres
	nr_allocs = max((loff_t)(off + len),(loff_t)inode->i_size) / OUICHEFS_BLOCK_SIZE;

	if (nr_allocs > inode->i_blocks - 1)
		nr_allocs -= inode->i_blocks - 1;
	else
		nr_allocs = 0;

	if (nr_allocs > sbi->nr_free_blocks) {
		inode_unlock(inode);
		return -ENOSPC;
	}

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		pr_err("Failed to read inode block %d\n", ci->index_block);
		inode_unlock(inode);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;
	struct ouichefs_extent* extents = index->blocks;

	// -----------------> Q1.9: SPARSE FILE HOLE INJECTION LOGIC
	uint32_t old_blocks = (inode->i_size + OUICHEFS_BLOCK_SIZE - 1) / OUICHEFS_BLOCK_SIZE;
	uint32_t new_start_block = off / OUICHEFS_BLOCK_SIZE;

	// Did the user seek past the end of the file, creating a gap?
	if (new_start_block > old_blocks) {
		uint32_t hole_blocks = new_start_block - old_blocks;
		int i, last_ext_idx = -1;

		// find the last valid extent in the array 
		for (i = 0; i < OUICHEFS_MAX_EXTENTS; i++) {
			if (extents[i].count == 0) break;
			last_ext_idx = i;
		}

		// ceck if the last extent is ALSO a hole, so we can merge them!
		if (last_ext_idx >= 0 && extents[last_ext_idx].start == 0) {
			extents[last_ext_idx].count += hole_blocks;
		} else {
			// create a brand new hole extent slot
			int new_ext_idx = last_ext_idx + 1;
			if (new_ext_idx < OUICHEFS_MAX_EXTENTS) {
				extents[new_ext_idx].start = 0; /* 0 means HOLE */
				extents[new_ext_idx].count = hole_blocks;
			} else {
				pr_err("ouichefs: Out of extent slots while injecting hole!\n");
				brelse(bh_index);
				inode_unlock(inode);
				return -ENOSPC;
			}
		}
		
		// save the injected hole to disk before we start writing real data
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);
	}
	/* ----------------------------------*/

	// On calcul l'indice du premier bloc à écrire
	iblock = off / OUICHEFS_BLOCK_SIZE;
	off = off % OUICHEFS_BLOCK_SIZE;

	for (; to_write > 0; iblock++) {
		size_t to_copy = OUICHEFS_BLOCK_SIZE - off;

		if (to_write < to_copy)
			to_copy = to_write;

		block = ouichefs_extent_get_block(extents, iblock, NULL); 

		if (block == 0) {
			struct buffer_head bh_tmp = {0};
			write_error = ouichefs_file_get_block(inode, iblock, &bh_tmp, 1, &nr_allocs); // On alloue le bloc (1.6.2 rajout de nr_allocs)
			if (write_error){
				pr_err("Failed to allocate new data block\n");
				break;
			}
				
			block = bh_tmp.b_blocknr;

			bh_data = sb_getblk(sb, block);

			if (!bh_data) {
				pr_err("Failed to get data block %u\n", block);
				write_error = -EIO;
				break;
			}

			// Nettoyage du buffer récupéré
			memset(bh_data->b_data, 0, OUICHEFS_BLOCK_SIZE);
			set_buffer_uptodate(bh_data);
		} else {
			bh_data = sb_bread(sb, block);

			if (!bh_data) {
				pr_err("Failed to get data block %u\n", block);
				write_error = -EIO;
				break;
			}
		}

		size_t ret = (size_t)copy_from_user((bh_data->b_data + off), (buf + bytes_copied), to_copy);

		to_write -= (to_copy - ret);
		bytes_copied += (to_copy - ret);
		off = 0;

		mark_buffer_dirty(bh_data);
		brelse(bh_data);

		if (ret) {
			write_error = -EFAULT;
			break;
		}
	}

	brelse(bh_index);

	if (bytes_copied == 0 && write_error) {
		inode_unlock(inode);
		return write_error;
	}
		

	*offset += bytes_copied;

	// Update inode metadata (A revoir)
	inode->i_size = (*offset > inode->i_size) ? *offset : inode->i_size;
	inode->i_blocks = (roundup(inode->i_size, OUICHEFS_BLOCK_SIZE) / OUICHEFS_BLOCK_SIZE) + 1;
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

	inode_unlock(inode);

	return bytes_copied;
}

/* 1.7.3 Releasing unused reservations */
static int ouichefs_release(struct inode *inode, struct file *file)
{
    struct ouichefs_sb_info *sbi = OUICHEFS_SB(inode->i_sb);
    struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);

    // Si le fichier possède encore des blocs pré-réservés en mémoire
    if (ci->i_reserved_count > 0) {

        // On libère les blocs un par un dans la bitmap
        for (uint32_t i = 0; i < ci->i_reserved_count; i++) {
            put_block(sbi, ci->i_reserved_start + i);
        }

        // On remet la fenêtre à zéro
        ci->i_reserved_start = 0;
        ci->i_reserved_count = 0;
    }

    return 0;
}

const struct file_operations ouichefs_file_ops = {
	.owner = THIS_MODULE,
	.open = ouichefs_open,
	.llseek = generic_file_llseek,
	//.read_iter = generic_file_read_iter,
	//.write_iter = generic_file_write_iter,
	.fsync = generic_file_fsync,
	.read = ouichefs_read,	// Q 1.2 → Q 1.4
	.write = ouichefs_write,	// Q 1.2 → Q 1.5
	.unlocked_ioctl = ouichefs_ioctl,	// Q 1.3
	.release = ouichefs_release,	// Q 1.7.3
};
