#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/linkage.h>

struct vfsmount;

struct open_intent {
	int	flags;
	int	create_mode;
	struct file *file;
};

//�������ӵ�Ƕ�׸��������ֵ
enum { MAX_NESTED_LINKS = 8 };

struct nameidata {
	/* Ŀ¼�����ĵ�ַ */	
	struct dentry	*dentry;
	/* �Ѱ�װ�ļ�ϵͳ����ĵ�ַ */
	struct vfsmount *mnt;
	//·���������һ������
	struct qstr	last;
	unsigned int	flags;
	//·���������һ������������
	int		last_type;
	//�������ӵ�Ƕ�׼���
	unsigned	depth;
	 /* ��Ƕ�׵ķ������ӹ�����·�������� */
	char *saved_names[MAX_NESTED_LINKS + 1];

	/* Intent data */
	  /* Intent data ������Ա�����壬ָ����η����ļ� */
	union {
		struct open_intent open;
	} intent;
};

/*
 * Type of the last component on LOOKUP_PARENT
 */
/* 
LAST_NORM�����һ����������ͨ�ļ���
LAST_ROOT�����һ�������ǡ�/����Ҳ��������·����Ϊ��/����
LAST_DOT�����һ�������ǡ�.��
LAST_DOTDOT�����һ�������ǡ�..��
LAST_BIND�����һ�����������ӵ������ļ�ϵͳ�ķ�������
*/

enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path compnents" flag
 *  - locked when lookup done with dcache_lock held
 *  - dentry cache is untrusted; force a real lookup
 */

//������һ�������Ƿ������ӣ�����ͣ�׷�٣���
#define LOOKUP_FOLLOW		 1
//���һ��������Ŀ¼
#define LOOKUP_DIRECTORY	 2
//��·�����л����ļ���Ҫ���
#define LOOKUP_CONTINUE		 4
//�������һ�����������ڵ�Ŀ¼
#define LOOKUP_PARENT		16
//������ģ���Ŀ¼����80x86��ϵ�ṹ��û���ã�
#define LOOKUP_NOALT		32
#define LOOKUP_REVAL		64
/*
 * Intent data
 */
//��ͼ��һ���ļ�
#define LOOKUP_OPEN		(0x0100)
//��ͼ����һ���ļ�����������ڣ�
#define LOOKUP_CREATE		(0x0200)
//��ͼΪһ���ļ�����û���Ȩ��
#define LOOKUP_ACCESS		(0x0400)

extern int FASTCALL(__user_walk(const char __user *, unsigned, struct nameidata *));
extern int FASTCALL(__user_walk_fd(int dfd, const char __user *, unsigned, struct nameidata *));
#define user_path_walk(name,nd) \
	__user_walk_fd(AT_FDCWD, name, LOOKUP_FOLLOW, nd)
#define user_path_walk_link(name,nd) \
	__user_walk_fd(AT_FDCWD, name, 0, nd)
extern int FASTCALL(path_lookup(const char *, unsigned, struct nameidata *));
extern int FASTCALL(path_walk(const char *, struct nameidata *));
extern int FASTCALL(link_path_walk(const char *, struct nameidata *));
extern void path_release(struct nameidata *);
extern void path_release_on_umount(struct nameidata *);

extern int __user_path_lookup_open(const char __user *, unsigned lookup_flags, struct nameidata *nd, int open_flags);
extern int path_lookup_open(int dfd, const char *name, unsigned lookup_flags, struct nameidata *, int open_flags);
extern struct file *lookup_instantiate_filp(struct nameidata *nd, struct dentry *dentry,
		int (*open)(struct inode *, struct file *));
extern struct file *nameidata_to_filp(struct nameidata *nd, int flags);
extern void release_open_intent(struct nameidata *);

extern struct dentry * lookup_one_len(const char *, struct dentry *, int);

extern int follow_down(struct vfsmount **, struct dentry **);
extern int follow_up(struct vfsmount **, struct dentry **);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

static inline void nd_set_link(struct nameidata *nd, char *path)
{
	nd->saved_names[nd->depth] = path;
}

static inline char *nd_get_link(struct nameidata *nd)
{
	return nd->saved_names[nd->depth];
}

#endif /* _LINUX_NAMEI_H */
