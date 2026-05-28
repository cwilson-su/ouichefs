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

/* 1.7.4 Garbage collector */
static void ouichefs_garbage_collector(struct super_block* sb)
{
	struct ouichefs_sb_info* sbi = OUICHEFS_SB(sb);
	struct inode* inode;

	sbi->gc_runs++;

	spin_lock(&sb->s_inode_list_lock);

	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		
		struct ouichefs_inode_info* ci = OUICHEFS_INODE(inode);
		uint32_t start, count;

		spin_lock(&inode->i_lock);
        start = ci->i_reserved_start;
        count = ci->i_reserved_count;
        /* Zéro atomiquement sous verrou — plus de double free possible */
        ci->i_reserved_start = 0;
        ci->i_reserved_count = 0;
        spin_unlock(&inode->i_lock);
		
		/* Libération HORS des deux verrous pour éviter deadlock */
        if (count > 0) {
            for (uint32_t i = 0; i < count; i++)
                put_block(sbi, start + i);
        }
	}

	spin_unlock(&sb->s_inode_list_lock);
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

		spin_lock(&inode->i_lock);
		if (ci->i_reserved_count) {
			if (*requested > ci->i_reserved_count) {
				bno = ci->i_reserved_start;
				index->blocks[last_extent_id].count += ci->i_reserved_count;
				*requested -= ci->i_reserved_count;
				ci->i_reserved_start += ci->i_reserved_count;
				ci->i_reserved_count = 0;	
			} else {
				bno = ci->i_reserved_start;
				index->blocks[last_extent_id].count += *requested;
				ci->i_reserved_start += *requested;
				ci->i_reserved_count -= *requested;
				*requested = 0;
			}

			spin_unlock(&inode->i_lock);
			goto dirty_index;
		}
		spin_unlock(&inode->i_lock);

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

		spin_lock(&inode->i_lock);
		ci->i_reserved_start = bno + allocated_block - new_window;
		ci->i_reserved_count = new_window;
		spin_unlock(&inode->i_lock);

		// Si le bloc récupéré est contigue au dernier extents count + 1
		if (bno == index->blocks[last_extent_id].start + index->blocks[last_extent_id].count) {
			index->blocks[last_extent_id].count += allocated_block - new_window;
		} else {
			if (current_extent_id == OUICHEFS_MAX_EXTENTS) {
				ret = -ENOSPC;
				goto brelse_index;
			}

			// Si on est dans un trou
			if (index->blocks[last_extent_id].count != 0) {
				uint32_t extent_logical_start = 0;
				uint32_t offset;
				uint32_t insert_count = allocated_block - new_window;

				/* 1. Calculate our exact position inside the hole */
				for (int i = 0; i < current_extent_id; i++) {
					extent_logical_start += index->blocks[i].count;
				}

				uint32_t extent_logical_end = extent_logical_start + index->blocks[current_extent_id].count;

				if (logical_block  == extent_logical_start) {
					if (insert_count == index->blocks[current_extent_id].count) {
						index->blocks[current_extent_id].start = bno;
						index->blocks[current_extent_id].count = insert_count;
						goto dirty_index;
					}
					
					if (index->blocks[OUICHEFS_MAX_EXTENTS - 1].start != 0) {
						ret = -ENOSPC;
						goto brelse_index;
					}

					for (int i = OUICHEFS_MAX_EXTENTS - 1; i > current_extent_id; i--) {
						index->blocks[i] = index->blocks[i - 1];
						if (i == current_extent_id + 1) {
							index->blocks[i].count -= insert_count;
						}
					}

					index->blocks[current_extent_id].start = bno;
					index->blocks[current_extent_id].count = insert_count;
					goto dirty_index;
				}

				if (logical_block  == extent_logical_end) {
					if (index->blocks[OUICHEFS_MAX_EXTENTS - 1].start != 0) {
						ret = -ENOSPC;
						goto brelse_index;
					}

					for (int i = OUICHEFS_MAX_EXTENTS - 1; i > current_extent_id; i--) {
						index->blocks[i] = index->blocks[i - 1];
						if (i == current_extent_id + 1) {
							index->blocks[i].start = bno;
							index->blocks[i].count = insert_count;
						}
					}

					index->blocks[current_extent_id].count -= insert_count;
					goto dirty_index;
				}

				if (logical_block > extent_logical_start && logical_block  < extent_logical_end) {
					offset = logical_block - extent_logical_start;
					
					if (offset + insert_count == extent_logical_end) {
							if (index->blocks[OUICHEFS_MAX_EXTENTS - 1].start != 0) {
							ret = -ENOSPC;
							goto brelse_index;
						}

						for (int i = OUICHEFS_MAX_EXTENTS - 1; i > current_extent_id; i--) {
							index->blocks[i] = index->blocks[i - 1];
							if (i == current_extent_id + 1) {
								index->blocks[i].start = bno;
								index->blocks[i].count = insert_count;
							}
						}

						index->blocks[current_extent_id].count = offset;
						goto dirty_index;
					}
					
					if (index->blocks[OUICHEFS_MAX_EXTENTS - 2].start != 0) {
						ret = -ENOSPC;
						goto brelse_index;
					}

					for (int i = OUICHEFS_MAX_EXTENTS - 1; i > current_extent_id; i--) {
						index->blocks[i] = index->blocks[i - 2];
						if (i == current_extent_id + 2) {
							index->blocks[i].start = 0;
							index->blocks[i].count = extent_logical_end - offset - insert_count;
						}

						if (i == current_extent_id + 1) {
							index->blocks[i].start = bno;
							index->blocks[i].count = insert_count;
						}
					}

					index->blocks[current_extent_id].count = offset;
					goto dirty_index;
				}
			} else {
				index->blocks[current_extent_id].start = bno;
				index->blocks[current_extent_id].count = allocated_block - new_window;;
			}
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

		for (iblock = 0; index->blocks[iblock].count != 0; iblock++) {
			if (index->blocks[iblock].start) {
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
	
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

	inode_unlock(inode);
	pr_info("ouichefs: Defragmented inode %lu into %u blocks at physical block %u\n", inode->i_ino, total_blocks, new_start);
	
	return 0;
}

/* 1.3.2 Debugging ioctl */
#define OUICHEFS_IOC_GET_EXTENTS _IO('O', 1)
#define OUICHEFS_IOC_DEFRAG_FILE _IO('O', 2) //Q1.10

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

	inode_lock_shared(inode);

	if (off >= inode->i_size) {
		inode_unlock_shared(inode);
		return 0; // Fin de fichier (EOF)
	}
        
	if (off + len > inode->i_size)
		to_read = inode->i_size - off;

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		inode_unlock_shared(inode);
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

	inode_unlock_shared(inode);

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
	sector_t current_extent_id;

	// Protection obligatoire contre les accès concurrents sur le même inode
    inode_lock(inode);

	if (file->f_flags & O_APPEND)
		*offset = inode->i_size;

	off = *offset;

	// On vérifie qu'il y a assez de blocks libres
	nr_allocs = DIV_ROUND_UP(max((loff_t)(off + len),(loff_t)inode->i_size), OUICHEFS_BLOCK_SIZE);

	if (nr_allocs > inode->i_blocks - 1)
		nr_allocs -= (inode->i_blocks - 1);
	else
		nr_allocs = 0;

	spin_lock(&sbi->bitmap_lock);
	if (nr_allocs > sbi->nr_free_blocks) {
		spin_unlock(&sbi->bitmap_lock);
		inode_unlock(inode);
		return -ENOSPC;
	}
	spin_unlock(&sbi->bitmap_lock);

	// Read index block from disk 
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index){
		pr_err("Failed to read inode block %d\n", ci->index_block);
		inode_unlock(inode);
		return -EIO;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;
	struct ouichefs_extent* extents = index->blocks;

	// On calcul l'indice du premier bloc à écrire
	iblock = off / OUICHEFS_BLOCK_SIZE;
	off = off % OUICHEFS_BLOCK_SIZE;

	// Ajout d'un trou si on écrit après la fin du fichier
	if (*offset > inode->i_size + OUICHEFS_BLOCK_SIZE) {
		ouichefs_extent_get_block(extents, iblock, &current_extent_id);
		sector_t last_extent_id = current_extent_id - 1;
		if (last_extent_id > 0) {
			if (extents[last_extent_id].start == 0 && extents[last_extent_id].count != 0) {
				extents[last_extent_id].count += iblock - inode->i_blocks;
				mark_buffer_dirty(bh_index);
				sync_dirty_buffer(bh_index);
				goto write_start;
			} 
		}

		if (current_extent_id == OUICHEFS_MAX_EXTENTS) {
			pr_err("ouichefs: Out of extent slots while injecting hole!\n");
			brelse(bh_index);
			inode_unlock(inode);
			return -ENOSPC;
		}
		
		extents[current_extent_id] = (struct ouichefs_extent) {0, iblock - inode->i_blocks};
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);
	}

write_start:

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

	uint64_t nb_block = 0;

	for (uint32_t i = 0; extents[i].count != 0; i++) {
		if (extents[i].start != 0)
			nb_block += extents[i].count;
	}

	brelse(bh_index);

	if (bytes_copied == 0 && write_error) {
		inode_unlock(inode);
		return write_error;
	}
		

	*offset += bytes_copied;

	// Update inode metadata (A revoir)
	inode->i_size = (*offset > inode->i_size) ? *offset : inode->i_size;
	inode->i_blocks = nb_block + 1;
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
	uint32_t start, count;

	spin_lock(&inode->i_lock);
    start = ci->i_reserved_start;
    count = ci->i_reserved_count;
    ci->i_reserved_start = 0;   /* zéro sous verrou = pas de double free */
    ci->i_reserved_count = 0;
    spin_unlock(&inode->i_lock);

    // Si le fichier possède encore des blocs pré-réservés en mémoire
    if (count > 0) {
		for (uint32_t i = 0; i < count; i++)
				put_block(sbi, start + i);
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
