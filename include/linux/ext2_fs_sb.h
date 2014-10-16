/*
 *  linux/include/linux/ext2_fs_sb.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs_sb.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _LINUX_EXT2_FS_SB
#define _LINUX_EXT2_FS_SB

#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>

/*
 * second extended-fs super-block data in memory
 */
struct ext2_sb_info {
	unsigned long s_frag_size;	/* Size of a fragment in bytes */
	unsigned long s_frags_per_block;/* Number of fragments per block */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_frags_per_group;/* Number of fragments in a group */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	unsigned long s_groups_count;	/* Number of groups in the fs */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
    //磁盘中的sb信息会读取到s_es中
	struct ext2_super_block * s_es;	/* Pointer to the super block in the buffer */
    //一个buffer_head *数组，保存组描述符表
	struct buffer_head ** s_group_desc;
	//保存了挂载选项, mount flags
	unsigned long  s_mount_opt;
	uid_t s_resuid;
	gid_t s_resgid;
	//当前挂载状态
	unsigned short s_mount_state;
	unsigned short s_pad;
    //块地址编号的位bit数, 也就是一个block可以容纳2 ^ s_addr_per_block_bits个块地址
	int s_addr_per_block_bits;
    //每个block中组描述符数
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	//目录总数
	unsigned long s_dir_count;
	//指向一个数组,orlov分配器使用
	u8 *s_debts;
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock s_blockgroup_lock;
};

#endif	/* _LINUX_EXT2_FS_SB */
