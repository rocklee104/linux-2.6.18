/*
 * mm/pdflush.c - worker threads for writing back filesystem data
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * 09Apr2002	akpm@zip.com.au
 *		Initial version
 * 29Feb2004	kaos@sgi.com
 *		Move worker thread creation to kthread to avoid chewing
 *		up stack space with nested calls to kernel_thread.
 */

#include <linux/sched.h>
#include <linux/list.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>		// Needed by writeback.h
#include <linux/writeback.h>	// Prototypes pdflush_operation()
#include <linux/kthread.h>
#include <linux/cpuset.h>


/*
 * Minimum and maximum number of pdflush instances
 */
#define MIN_PDFLUSH_THREADS	2
#define MAX_PDFLUSH_THREADS	8

static void start_one_pdflush_thread(void);


/*
 * The pdflush threads are worker threads for writing back dirty data.
 * Ideally, we'd like one thread per active disk spindle.  But the disk
 * topology is very hard to divine at this level.   Instead, we take
 * care in various places to prevent more than one pdflush thread from
 * performing writeback against a single filesystem.  pdflush threads
 * have the PF_FLUSHER flag set in current->flags to aid in this.
 */

/*
 * All the pdflush threads.  Protected by pdflush_lock
 */
//链表中最末的线程，睡眠时间最长
static LIST_HEAD(pdflush_list);
static DEFINE_SPINLOCK(pdflush_lock);

/*
 * The count of currently-running pdflush threads.  Protected
 * by pdflush_lock.
 *
 * Readable by sysctl, but not writable.  Published to userspace at
 * /proc/sys/vm/nr_pdflush_threads.
 */
//并发pdflush线程的数目
int nr_pdflush_threads = 0;

/*
 * The time at which the pdflush thread pool last went empty
 */
//最近一次链表为空的时间
static unsigned long last_empty_jifs;

/*
 * The pdflush thread.
 *
 * Thread pool management algorithm:
 * 
 * - The minimum and maximum number of pdflush instances are bound
 *   by MIN_PDFLUSH_THREADS and MAX_PDFLUSH_THREADS.
 * 
 * - If there have been no idle pdflush instances for 1 second, create
 *   a new one.
 * 
 * - If the least-recently-went-to-sleep pdflush thread has been asleep
 *   for more than one second, terminate a thread.
 */

/*
 * A structure for passing work to a pdflush thread.  Also for passing
 * state information between pdflush threads.  Protected by pdflush_lock.
 */
struct pdflush_work {
	//指向task_struct实例
	struct task_struct *who;	/* The thread */
	void (*fn)(unsigned long);	/* A callback function */
	unsigned long arg0;		/* An argument to the callback */
	//链表元素,用于在线程空闲时,将线程置于pdflush_list链表上
	struct list_head list;		/* On pdflush_list, when idle */
	/*
	 *上一次进入睡眠的时间,用于从系统中删除多余的pdflush线程( 
	 *即在内存中已经有一段比较长的时间处于空闲状态的线程)
	*/
	unsigned long when_i_went_to_sleep;
};

static int __pdflush(struct pdflush_work *my_work)
{
	current->flags |= PF_FLUSHER | PF_SWAPWRITE;
	my_work->fn = NULL;
	my_work->who = current;
	INIT_LIST_HEAD(&my_work->list);

	spin_lock_irq(&pdflush_lock);
	nr_pdflush_threads++;
	for ( ; ; ) {
		struct pdflush_work *pdf;

		set_current_state(TASK_INTERRUPTIBLE);
		list_move(&my_work->list, &pdflush_list);
		my_work->when_i_went_to_sleep = jiffies;
		spin_unlock_irq(&pdflush_lock);
		schedule();
		//检查是否要进入电源睡眠状态，并进行处理
		try_to_freeze();
		spin_lock_irq(&pdflush_lock);
		//被唤醒之前已经把my_work从pdflush_list上取下来了
		if (!list_empty(&my_work->list)) {
			/*
			 * Someone woke us up, but without removing our control
			 * structure from the global list.  swsusp will do this
			 * in try_to_freeze()->refrigerator().  Handle it.
			 */
			my_work->fn = NULL;
			continue;
		}
		if (my_work->fn == NULL) {
			printk("pdflush: bogus wakeup\n");
			continue;
		}
		spin_unlock_irq(&pdflush_lock);

		(*my_work->fn)(my_work->arg0);

		/*
		 * Thread creation: For how long have there been zero
		 * available threads?
		 */
		if (jiffies - last_empty_jifs > 1 * HZ) {
			/*
			 * 如果当前的时间比上次链表为空时间晚1hz的时间,
			 * 也就是说pdflush线程全在忙碌,并且忙碌的时间超过1s
			*/
			/* unlocked list_empty() test is OK here */
			if (list_empty(&pdflush_list)) {
				//如果当前pdflush_list链表为空，说明当前的已经没有正在睡眠的pdflush线程
				/* unlocked test is OK here */
				if (nr_pdflush_threads < MAX_PDFLUSH_THREADS)
					start_one_pdflush_thread();
			}
		}

		spin_lock_irq(&pdflush_lock);
		my_work->fn = NULL;

		/*
		 * Thread destruction: For how long has the sleepiest
		 * thread slept?
		 */
		if (list_empty(&pdflush_list))
			continue;
		//pdflush_list上有work空闲
		if (nr_pdflush_threads <= MIN_PDFLUSH_THREADS)
			//如果pdflush个数小于等于MIN_PDFLUSH_THREADS,就不能退出,继续工作
			continue;
		//如果pdflush个数大于MIN_PDFLUSH_THREADS
		pdf = list_entry(pdflush_list.prev, struct pdflush_work, list);
		if (jiffies - pdf->when_i_went_to_sleep > 1 * HZ) {
			//取出pdflush_list链表中最后一个成员,如果其休眠时间超过1s
			/* Limit exit rate */
			pdf->when_i_went_to_sleep = jiffies;
			break;					/* exeunt */
		}
	}
	//线程退出
	nr_pdflush_threads--;
	spin_unlock_irq(&pdflush_lock);
	return 0;
}

/*
 * Of course, my_work wants to be just a local in __pdflush().  It is
 * separated out in this manner to hopefully prevent the compiler from
 * performing unfortunate optimisations against the auto variables.  Because
 * these are visible to other tasks and CPUs.  (No problem has actually
 * been observed.  This is just paranoia).
 */
static int pdflush(void *dummy)
{
	struct pdflush_work my_work;
	cpumask_t cpus_allowed;

	/*
	 * pdflush can spend a lot of time doing encryption via dm-crypt.  We
	 * don't want to do that at keventd's priority.
	 */
	set_user_nice(current, 0);

	/*
	 * Some configs put our parent kthread in a limited cpuset,
	 * which kthread() overrides, forcing cpus_allowed == CPU_MASK_ALL.
	 * Our needs are more modest - cut back to our cpusets cpus_allowed.
	 * This is needed as pdflush's are dynamically created and destroyed.
	 * The boottime pdflush's are easily placed w/o these 2 lines.
	 */
	cpus_allowed = cpuset_cpus_allowed(current);
	set_cpus_allowed(current, cpus_allowed);

	return __pdflush(&my_work);
}

/*
 * Attempt to wake up a pdflush thread, and get it to do some work for you.
 * Returns zero if it indeed managed to find a worker thread, and passed your
 * payload to it.
 */
int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0)
{
	unsigned long flags;
	int ret = 0;

	BUG_ON(fn == NULL);	/* Hard to diagnose if it's deferred */

	spin_lock_irqsave(&pdflush_lock, flags);
	if (list_empty(&pdflush_list)) {
		spin_unlock_irqrestore(&pdflush_lock, flags);
		ret = -1;
	} else {
		struct pdflush_work *pdf;

		pdf = list_entry(pdflush_list.next, struct pdflush_work, list);
		list_del_init(&pdf->list);
		if (list_empty(&pdflush_list))
			//一旦pdflush_list空，就给last_empty_jifs赋值
			last_empty_jifs = jiffies;
		pdf->fn = fn;
		pdf->arg0 = arg0;
		wake_up_process(pdf->who);
		spin_unlock_irqrestore(&pdflush_lock, flags);
	}
	return ret;
}

static void start_one_pdflush_thread(void)
{
	kthread_run(pdflush, NULL, "pdflush");
}

static int __init pdflush_init(void)
{
	int i;

	//启动2个pdflush线程
	for (i = 0; i < MIN_PDFLUSH_THREADS; i++)
		start_one_pdflush_thread();
	return 0;
}

module_init(pdflush_init);
