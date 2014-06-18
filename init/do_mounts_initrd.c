#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/romfs_fs.h>
#include <linux/initrd.h>
#include <linux/sched.h>

#include "do_mounts.h"

unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;
//真正文件系统的设备号
unsigned int real_root_dev;	/* do_proc_dointvec cannot handle kdev_t */
static int __initdata old_fd, root_fd;
static int __initdata mount_initrd = 1;

static int __init no_initrd(char *str)
{
	mount_initrd = 0;
	return 1;
}

__setup("noinitrd", no_initrd);

static int __init do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };
	extern char * envp_init[];

	sys_close(old_fd);sys_close(root_fd);
	sys_close(0);sys_close(1);sys_close(2);
	sys_setsid();
	(void) sys_open("/dev/console",O_RDWR,0);
	(void) sys_dup(0);
	(void) sys_dup(0);
	return execve(shell, argv, envp_init);
}

static void __init handle_initrd(void)
{
	int error;
	int pid;

	real_root_dev = new_encode_dev(ROOT_DEV);
	create_dev("/dev/root.old", Root_RAM0);
	/* mount initrd on rootfs' /root */
	//将initrd文件挂载到/root下
	mount_block_root("/dev/root.old", root_mountflags & ~MS_RDONLY);
	sys_mkdir("/old", 0700);
	root_fd = sys_open("/", 0, 0);
	old_fd = sys_open("/old", 0, 0);
	/* move initrd over / and chdir/chroot in initrd root */
	//chroot进入initrd的文件系统
	sys_chdir("/root");
	sys_mount(".", "/", NULL, MS_MOVE, NULL);
	sys_chroot(".");

	current->flags |= PF_NOFREEZE;
	//执行initrd的/linuxrc文件,等待其结束
	pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
	if (pid > 0) {
		while (pid != sys_wait4(-1, NULL, 0, NULL))
			yield();
	}

	/* move initrd to rootfs' /old */
	sys_fchdir(old_fd);
	sys_mount("/", ".", NULL, MS_MOVE, NULL);
	/* switch root and cwd back to / of rootfs */
	sys_fchdir(root_fd);
	sys_chroot(".");
	sys_close(old_fd);
	sys_close(root_fd);

	if (new_decode_dev(real_root_dev) == Root_RAM0) {
		sys_chdir("/old");
		return;
	}

	ROOT_DEV = new_decode_dev(real_root_dev);
	mount_root();

	printk(KERN_NOTICE "Trying to move old root to /initrd ... ");
	error = sys_mount("/old", "/root/initrd", NULL, MS_MOVE, NULL);
	if (!error)
		printk("okay\n");
	else {
		int fd = sys_open("/dev/root.old", O_RDWR, 0);
		if (error == -ENOENT)
			printk("/initrd does not exist. Ignored.\n");
		else
			printk("failed\n");
		printk(KERN_NOTICE "Unmounting old root\n");
		sys_umount("/old", MNT_DETACH);
		printk(KERN_NOTICE "Trying to free ramdisk memory ... ");
		if (fd < 0) {
			error = fd;
		} else {
			error = sys_ioctl(fd, BLKFLSBUF, 0);
			sys_close(fd);
		}
		printk(!error ? "okay\n" : "failed\n");
	}
}

int __init initrd_load(void)
{
	if (mount_initrd) {
		//建立一个ram0设备
		create_dev("/dev/ram", Root_RAM0);
		/*
		 * Load the initrd data into /dev/ram0. Execute it as initrd
		 * unless /dev/ram0 is supposed to be our actual root device,
		 * in that case the ram disk is just set up here, and gets
		 * mounted in the normal path.
		 */
		 //如果rd_load_image执行成功的话,会将真正文件系统的根目录设置为当前目录
		 ///initrd.image保存的就是image-initrd, rd_load_image将/initrd.image的内容
		 //释放到ram0中,ROOT_DEV的含义是,如果在grub中配置了root=/dev/ram0,它是实际
		 //就是真正的文件系统了
		if (rd_load_image("/initrd.image") && ROOT_DEV != Root_RAM0) {
			sys_unlink("/initrd.image");
			handle_initrd();
			return 1;
		}
	}
	sys_unlink("/initrd.image");
	return 0;
}
