/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

struct super_block;
struct vfsmount;
struct dentry;
struct namespace;

#define MNT_NOSUID	0x01
//Do not interpret special files on the file system
#define MNT_NODEV	0x02
#define MNT_NOEXEC	0x04
#define MNT_NOATIME	0x08
#define MNT_NODIRATIME	0x10

#define MNT_SHRINKABLE	0x100

#define MNT_SHARED	0x1000	/* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE	0x2000	/* if the vfsmount is a unbindable mount */
#define MNT_PNODE_MASK	0x3000	/* propogation flag mask */

struct vfsmount {
	//���ڽ���ͬhashֵ��vfsmount�������,hashͷ��mount_hashtable
	struct list_head mnt_hash;
	//���ص����ڵĸ��ļ�ϵͳ
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	//���ص��ڸ��ļ�ϵͳ�е�dentry 
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
	//��ǰ�ļ�ϵͳ��Ŀ¼��dentry
	struct dentry *mnt_root;	/* root of the mounted tree */
	//ָ�򳬼����ָ��
	struct super_block *mnt_sb;	/* pointer to superblock */
	//�����������ļ�ϵͳ������ͷ, ��Ա�����ļ�ϵͳ��mnt_child
	struct list_head mnt_mounts;	/* list of children, anchored here */
	//����ͷ�Ǹ�vfsmount��mnt_mounts�ĳ�Ա
	struct list_head mnt_child;	/* and going through their mnt_child */
	//�ж�����vfsmount
	atomic_t mnt_count;
	int mnt_flags;
	//�������,�˳�ԱΪ1
	int mnt_expiry_mark;		/* true if marked for expiry */
	//�豸����,��/dev/block/sda1
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */

	//����Ԫ��, ����ͷ��namespace->list
	struct list_head mnt_list;
	//����Ԫ��,���ļ�ϵͳ��������ĳ�Ա
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	//����Ԫ��,���ڹ�����ص�ѭ������,û������ͷ
	struct list_head mnt_share;	/* circular list of shared mounts */
	//�������ص�����ͷ,��Ա��mnt_slave
	struct list_head mnt_slave_list;/* list of slave mounts */
	//����Ԫ��,���ڴ�������,����ͷ��mnt_slave_list
	struct list_head mnt_slave;	/* slave list entry */
	//ָ��������,��������λ��master->mnt_slave_list 
	struct vfsmount *mnt_master;	/* slave is on master->mnt_slave_list */
	//�����������ռ�
	struct namespace *mnt_namespace; /* containing namespace */
	int mnt_pinned;
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern void mntput_no_expire(struct vfsmount *mnt);
extern void mnt_pin(struct vfsmount *mnt);
extern void mnt_unpin(struct vfsmount *mnt);

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		mntput_no_expire(mnt);
	}
}

extern void free_vfsmnt(struct vfsmount *mnt);
extern struct vfsmount *alloc_vfsmnt(const char *name);
extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);

struct file_system_type;
extern struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				      int flags, const char *name,
				      void *data);

struct nameidata;

extern int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);
extern void shrink_submounts(struct vfsmount *mountpoint, struct list_head *mounts);

extern spinlock_t vfsmount_lock;
extern dev_t name_to_dev_t(char *name);

#endif
#endif /* _LINUX_MOUNT_H */
