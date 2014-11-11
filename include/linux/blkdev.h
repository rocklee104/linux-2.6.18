#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/stringify.h>

#include <asm/scatterlist.h>

struct scsi_ioctl_command;

struct request_queue;
typedef struct request_queue request_queue_t;
struct elevator_queue;
typedef struct elevator_queue elevator_t;
struct request_pm_state;
struct blk_trace;

#define BLKDEV_MIN_RQ	4
#define BLKDEV_MAX_RQ	128	/* Default maximum */

/*
 * This is the per-process anticipatory I/O scheduler state.
 */
struct as_io_context {
	spinlock_t lock;

	void (*dtor)(struct as_io_context *aic); /* destructor */
	void (*exit)(struct as_io_context *aic); /* called on task exit */

	unsigned long state;
	atomic_t nr_queued; /* queued reads & sync writes */
	atomic_t nr_dispatched; /* number of requests gone to the drivers */

	/* IO History tracking */
	/* Thinktime */
	unsigned long last_end_request;
	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;
	/* Layout pattern */
	unsigned int seek_samples;
	sector_t last_request_pos;
	u64 seek_total;
	sector_t seek_mean;
};

struct cfq_queue;
struct cfq_io_context {
	struct rb_node rb_node;
	void *key;

	struct cfq_queue *cfqq[2];

	struct io_context *ioc;

	unsigned long last_end_request;
	sector_t last_request_pos;
 	unsigned long last_queue;

	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;

	unsigned int seek_samples;
	u64 seek_total;
	sector_t seek_mean;

	struct list_head queue_list;

	void (*dtor)(struct io_context *); /* destructor */
	void (*exit)(struct io_context *); /* called on task exit */
};

/*
 * This is the per-process I/O subsystem state.  It is refcounted and
 * kmalloc'ed. Currently all fields are modified in process io context
 * (apart from the atomic refcount), so require no locking.
 */
struct io_context {
	atomic_t refcount;
	struct task_struct *task;

	int (*set_ioprio)(struct io_context *, unsigned int);

	/*
	 * For request batching
	 */
	unsigned long last_waited; /* Time last woken after wait for request */
	int nr_batch_requests;     /* Number of requests left in the batch */

	struct as_io_context *aic;
	struct rb_root cic_root;
};

void put_io_context(struct io_context *ioc);
void exit_io_context(void);
struct io_context *current_io_context(gfp_t gfp_flags);
struct io_context *get_io_context(gfp_t gfp_flags);
void copy_io_context(struct io_context **pdst, struct io_context **psrc);
void swap_io_context(struct io_context **ioc1, struct io_context **ioc2);

struct request;
typedef void (rq_end_io_fn)(struct request *, int);

struct request_list {
	//读/写各占用一个count
	int count[2];
	int starved[2];
	int elvpriv;
	mempool_t *rq_pool;
	wait_queue_head_t wait[2];
};

#define BLK_MAX_CDB	16

/*
 * try to put the fields that are referenced together in the same cacheline
 */
struct request {
	//链表成员，链表头是struct request_queue的queue_head
	struct list_head queuelist;
	struct list_head donelist;
	//请求标志,比如QUEUE_FLAG_STOPPED
	unsigned long flags;		/* see REQ_ bits below */

	/* Maintain bio traversal state for part by part I/O submission.
	 * hard_* are block layer internals, no driver should touch them!
	 */
	//当前请求要求数据传输的块设备的起始扇区
	sector_t sector;		/* next sector to submit */
	//整个请求中要传送的扇区数
	unsigned long nr_sectors;	/* no. of sectors left to submit */
	/* no. of sectors left to submit in the current segment */
	//当前bio的当前段中要传送的扇区数
	unsigned int current_nr_sectors;
	//要传送的下一个扇区号
	sector_t hard_sector;		/* next sector to complete */
    //整个请求中要传送的扇区数(由通用块层更新)
	unsigned long hard_nr_sectors;	/* no. of sectors left to complete */
    /* no. of sectors left to complete in the current segment */
    //当前bio的当前段中要传送的扇区数(由通用块层更新)
	unsigned int hard_cur_sectors;
	//请求中第一个没有完成传送操作的bio
	struct bio *bio;
	//请求链表中末尾的bio
	struct bio *biotail;
	//指向io调度程序私有数据的指针
	void *elevator_private;
	void *completion_data;
	//请求状态：实际上，是RQ_ACTIVE或者是RQ_INACTIVE
	int rq_status;	/* should split this into a few status bits */
	//用于记录当前传送中发生的io失败次数的计数器
	int errors;
	struct gendisk *rq_disk;
	//请求的起始时间（用jiffies表示）
	unsigned long start_time;

	/* Number of scatter-gather DMA addr+len pairs after
	 * physical address coalescing is performed.
	 */
	//请求的物理段数
	unsigned short nr_phys_segments;

	/* Number of scatter-gather addr+len pairs after
	 * physical and DMA remapping hardware coalescing is performed.
	 * This is the number of scatter-gather entries the driver
	 * will actually have to deal with after DMA mapping is done.
	 */
	//请求的硬段数
	unsigned short nr_hw_segments;

	unsigned short ioprio;
	//与请求相关的标记（只适合多次数据传送的硬件设备）
	int tag;
	//请求的引用计数
	int ref_count;
	//指向包含请求的队列描述符的指针
	request_queue_t *q;
	//指向request_list结构的指针
	struct request_list *rl;
	//等待数据传送终止的completion结构
	struct completion *waiting;
	//对硬件设备发出“特殊”命令的请求所使用的数据的指针
	void *special;
	//指向当前数据传送的内存缓冲区的指针(如果缓冲区是高端内存区，则为null)
	char *buffer;

	/*
	 * when request is used as a packet command carrier
	 */
	//cmd字段中命令的长度
	unsigned int cmd_len;
	//由请求队列的prep_rq_fn方法准备好的预先内置命令所在的缓冲区
	unsigned char cmd[BLK_MAX_CDB];
	//通常,由data字段指向的缓冲区中数据的长度,表示当前请求要求数据传输的总的数据量
	unsigned int data_len;
	//由sense字段指向的缓冲区的长度，如果sense是null,则为0
	unsigned int sense_len;
	//设备驱动程序为了跟踪所传送的数据而使用的指针
	void *data;
	//指向输出sense命令的缓冲区的指针
	void *sense;
	//请求的超时
	unsigned int timeout;
	int retries;

	/*
	 * completion callback. end_io_data should be folded in with waiting
	 */
	rq_end_io_fn *end_io;
	void *end_io_data;
};

/*
 * first three bits match BIO_RW* bits, important
 */
enum rq_flag_bits {
	__REQ_RW,		/* not set, read. set, write */
	__REQ_FAILFAST,		/* no low level driver retries */
	__REQ_SORTED,		/* elevator knows about this request */
	__REQ_SOFTBARRIER,	/* may not be passed by ioscheduler */
	__REQ_HARDBARRIER,	/* may not be passed by drive either */
	__REQ_FUA,		/* forced unit access */
	__REQ_CMD,		/* is a regular fs rw request */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_STARTED,		/* drive already may have started this one */
	__REQ_DONTPREP,		/* don't call prep for this one */
	__REQ_QUEUED,		/* uses queueing */
	__REQ_ELVPRIV,		/* elevator private data attached */
	/*
	 * for ATA/ATAPI devices
	 */
	__REQ_PC,		/* packet command (special) */
	__REQ_BLOCK_PC,		/* queued down pc from block layer */
	__REQ_SENSE,		/* sense retrival */

	__REQ_FAILED,		/* set if the request failed */
	__REQ_QUIET,		/* don't worry about errors */
	__REQ_SPECIAL,		/* driver suplied command */
	__REQ_DRIVE_CMD,
	__REQ_DRIVE_TASK,
	__REQ_DRIVE_TASKFILE,
	__REQ_PREEMPT,		/* set for "ide_preempt" requests */
	__REQ_PM_SUSPEND,	/* suspend request */
	__REQ_PM_RESUME,	/* resume request */
	__REQ_PM_SHUTDOWN,	/* shutdown request */
	__REQ_ORDERED_COLOR,	/* is before or after barrier */
	__REQ_RW_SYNC,		/* request is sync (O_DIRECT) */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_RW		(1 << __REQ_RW)
#define REQ_FAILFAST	(1 << __REQ_FAILFAST)
#define REQ_SORTED	(1 << __REQ_SORTED)
#define REQ_SOFTBARRIER	(1 << __REQ_SOFTBARRIER)
#define REQ_HARDBARRIER	(1 << __REQ_HARDBARRIER)
#define REQ_FUA		(1 << __REQ_FUA)
#define REQ_CMD		(1 << __REQ_CMD)
#define REQ_NOMERGE	(1 << __REQ_NOMERGE)
#define REQ_STARTED	(1 << __REQ_STARTED)
#define REQ_DONTPREP	(1 << __REQ_DONTPREP)
#define REQ_QUEUED	(1 << __REQ_QUEUED)
#define REQ_ELVPRIV	(1 << __REQ_ELVPRIV)
#define REQ_PC		(1 << __REQ_PC)
#define REQ_BLOCK_PC	(1 << __REQ_BLOCK_PC)
#define REQ_SENSE	(1 << __REQ_SENSE)
#define REQ_FAILED	(1 << __REQ_FAILED)
#define REQ_QUIET	(1 << __REQ_QUIET)
#define REQ_SPECIAL	(1 << __REQ_SPECIAL)
#define REQ_DRIVE_CMD	(1 << __REQ_DRIVE_CMD)
#define REQ_DRIVE_TASK	(1 << __REQ_DRIVE_TASK)
#define REQ_DRIVE_TASKFILE	(1 << __REQ_DRIVE_TASKFILE)
#define REQ_PREEMPT	(1 << __REQ_PREEMPT)
#define REQ_PM_SUSPEND	(1 << __REQ_PM_SUSPEND)
#define REQ_PM_RESUME	(1 << __REQ_PM_RESUME)
#define REQ_PM_SHUTDOWN	(1 << __REQ_PM_SHUTDOWN)
#define REQ_ORDERED_COLOR	(1 << __REQ_ORDERED_COLOR)
#define REQ_RW_SYNC	(1 << __REQ_RW_SYNC)

/*
 * State information carried for REQ_PM_SUSPEND and REQ_PM_RESUME
 * requests. Some step values could eventually be made generic.
 */
struct request_pm_state
{
	/* PM state machine step value, currently driver specific */
	int	pm_step;
	/* requested PM state value (S1, S2, S3, S4, ...) */
	u32	pm_state;
	void*	data;		/* for driver use */
};

#include <linux/elevator.h>

typedef int (merge_request_fn) (request_queue_t *, struct request *,
				struct bio *);
typedef int (merge_requests_fn) (request_queue_t *, struct request *,
				 struct request *);
typedef void (request_fn_proc) (request_queue_t *q);
typedef int (make_request_fn) (request_queue_t *q, struct bio *bio);
typedef int (prep_rq_fn) (request_queue_t *, struct request *);
typedef void (unplug_fn) (request_queue_t *);

struct bio_vec;
typedef int (merge_bvec_fn) (request_queue_t *, struct bio *, struct bio_vec *);
typedef void (activity_fn) (void *data, int rw);
typedef int (issue_flush_fn) (request_queue_t *, struct gendisk *, sector_t *);
typedef void (prepare_flush_fn) (request_queue_t *, struct request *);
typedef void (softirq_done_fn)(struct request *);

enum blk_queue_state {
	Queue_down,
	Queue_up,
};

struct blk_queue_tag {
	struct request **tag_index;	/* map of busy tags */
	unsigned long *tag_map;		/* bit map of free/busy tags */
	struct list_head busy_list;	/* fifo list of busy tags */
	int busy;			/* current depth */
	int max_depth;			/* what we will send to device */
	int real_max_depth;		/* what the array can hold */
	atomic_t refcnt;		/* map can be shared */
};

struct request_queue
{
	/*
	 * Together with queue_head for cacheline sharing
	 */
	//待处理请求队列的链表, 链表头，成员是struct request中的queuelist
	struct list_head	queue_head;
	//指向队列中首先可能合并的请求描述符
	struct request		*last_merge;
	//指向elevator对象的指针
	elevator_t		*elevator;

	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	//为分配请求描述符所使用的数据结构
	struct request_list	rq;
	/*
	 * 指向块设备驱动程序需要实现的请求处理函数.当内核中的其他组件比如文件系统需要从
	 * 底层块设备读取或者写入数据时,如果设备驱动程序采用的是request方式实现的,内核会
	 * 调用该例程.因此该例程需要针对特定的设备实现底层的i/o操作.
	*/
	request_fn_proc		*request_fn;
	//检查是否可能将bio合并到请求队列的最后一个请求中的方法
	merge_request_fn	*back_merge_fn;
	//检查是否可能将bio合并到队列的第一个请求中的方法
	merge_request_fn	*front_merge_fn;
	//试图合并请求队列中两个相邻请求的方法
	merge_requests_fn	*merge_requests_fn;
	/*
	 * 如果驱动程序调用blk_init_queue来处理请求,那么在其调用链中,kernel会为当前请求队列
	 * 的make_request_fn提供一个标准的实现__make_request.但如果驱动程序采用所谓的make_request
	 * 方式实现,则驱动程序需要调用blk_queue_make_request为此处的make_request_fn提供一个实现,
	 * 由于blk_queue_make_request函数的内部不负责创建设备的请求队列,所以make_request方式
	 * 需要驱动程序调用blk_queue_make_request前显示地为设备创建一个请求队列.
	*/
	make_request_fn		*make_request_fn;
	//该方法把这个请求处理的命令发送给硬件设备
	prep_rq_fn		*prep_rq_fn;
	//去掉块设备的方法
	unplug_fn		*unplug_fn;
	//当增加一个新段时，该方法返回可插入到某个已存在的bio结构中的字节数
	merge_bvec_fn		*merge_bvec_fn;
	//将某个请求加入到请求队列时调用的方法
	activity_fn		*activity_fn;
	//刷新请求队列时调用的方法
	issue_flush_fn		*issue_flush_fn;
	prepare_flush_fn	*prepare_flush_fn;
    //软中断执行函数,由具体driver指定
	softirq_done_fn		*softirq_done_fn;

	/*
	 * Dispatch queue sorting
	 */
	sector_t		end_sector;
	struct request		*boundary_rq;

	/*
	 * Auto-unplugging state
	 */
	//插入设备时使用的动态定时器
	struct timer_list	unplug_timer;
	//如果请求队列中待处理请求数大于该值，将立即去掉请求设备
	int			unplug_thresh;	/* After this many requests */
	//去掉设备之前的延迟
	unsigned long		unplug_delay;	/* After this many jiffies */
	//去掉设备时使用的操作队列
	struct work_struct	unplug_work;
	//backing_dev_info存放了关于硬件块设备的i/o数据流量信息。
	//例如它保存了关于预读以及关于请求队列拥堵状态的信息
	struct backing_dev_info	backing_dev_info;

	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	//指向块设备驱动程序的私有数据的指针
	void			*queuedata;
	//activity_fn使用的私有数据
	void			*activity_data;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	//在大于该叶框号时必须使用缓冲区回弹
	unsigned long		bounce_pfn;
	//回弹缓冲区使用的内存分配标志
	gfp_t			bounce_gfp;

	/*
	 * various queue flags, see QUEUE_* below
	 */
	//描述请求队列状态的标志
	unsigned long		queue_flags;

	/*
	 * protects queue structures from reentrancy. ->__queue_lock should
	 * _never_ be used directly, it is queue private. always use
	 * ->queue_lock.
	 */
	spinlock_t		__queue_lock;
	//指向请求队列锁的指针
	spinlock_t		*queue_lock;

	/*
	 * queue kobject
	 */
	//请求队列内嵌kobject结构
	struct kobject kobj;

	/*
	 * queue settings
	 */
	//请求队列中允许的最大请求数
	unsigned long		nr_requests;	/* Max # of requests */
	//如果待处理请求数超出了该阀值，则认为该队列是拥挤的
	unsigned int		nr_congestion_on;
    //如果待处理请求数在该阀值内，则认为该队列是不拥挤的
	unsigned int		nr_congestion_off;
	//即使队列已满，仍可以由特殊进程"batch"提交的待处理请求队列的最大值(通常为32)
	unsigned int		nr_batching;
	//单个请求能处理的最大扇区数(可调的)
	unsigned int		max_sectors;
    //单个请求能处理的最大扇区数(硬件约束)
	unsigned int		max_hw_sectors;
	//单个请求所能处理的最大物理段数,当次request bio_vec的总数
    unsigned short		max_phys_segments;
    //单个请求所能处理的最大硬段数(分散-聚集DMA操作中的最大不同内存区数)
	unsigned short		max_hw_segments;
	//扇区中以字节为单位的大小，这是硬件中块的大小
	unsigned short		hardsect_size;
	//物理段的最大长度（以字节为单位）
	unsigned int		max_segment_size;
	//段合并的内存边界mask
	unsigned long		seg_boundary_mask;
	//DMA缓冲去的起始地址和长度的对齐位图（default 511）
	unsigned int		dma_alignment;
	//空闲/busy标记的位图（用于带标记的请求）
	struct blk_queue_tag	*queue_tags;

	unsigned int		nr_sorted;
	//请求队列中待处理的请求数
	unsigned int		in_flight;

	/*
	 * sg stuff
	 */
	//用户定义的命令超时（仅由scsi通用块设备使用）
	unsigned int		sg_timeout;
	//基本没有使用
	unsigned int		sg_reserved_size;
	int			node;

	struct blk_trace	*blk_trace;

	/*
	 * reserved for flush operations
	 */
	unsigned int		ordered, next_ordered, ordseq;
	int			orderr, ordcolor;
	struct request		pre_flush_rq, bar_rq, post_flush_rq;
	struct request		*orig_bar_rq;
	unsigned int		bi_size;

	struct mutex		sysfs_lock;
};

#define RQ_INACTIVE		(-1)
#define RQ_ACTIVE		1

#define QUEUE_FLAG_CLUSTER	0	/* cluster several segments into 1 */
#define QUEUE_FLAG_QUEUED	1	/* uses generic tag queueing */
#define QUEUE_FLAG_STOPPED	2	/* queue is stopped */
#define	QUEUE_FLAG_READFULL	3	/* write queue has been filled */
#define QUEUE_FLAG_WRITEFULL	4	/* read queue has been filled */
#define QUEUE_FLAG_DEAD		5	/* queue being torn down */
#define QUEUE_FLAG_REENTER	6	/* Re-entrancy avoidance */
#define QUEUE_FLAG_PLUGGED	7	/* queue is plugged */
#define QUEUE_FLAG_ELVSWITCH	8	/* don't use elevator, just do FIFO */

enum {
	/*
	 * Hardbarrier is supported with one of the following methods.
	 *
	 * NONE		: hardbarrier unsupported
	 * DRAIN	: ordering by draining is enough
	 * DRAIN_FLUSH	: ordering by draining w/ pre and post flushes
	 * DRAIN_FUA	: ordering by draining w/ pre flush and FUA write
	 * TAG		: ordering by tag is enough
	 * TAG_FLUSH	: ordering by tag w/ pre and post flushes
	 * TAG_FUA	: ordering by tag w/ pre flush and FUA write
	 */
	QUEUE_ORDERED_NONE	= 0x00,
	QUEUE_ORDERED_DRAIN	= 0x01,
	QUEUE_ORDERED_TAG	= 0x02,

	QUEUE_ORDERED_PREFLUSH	= 0x10,
	QUEUE_ORDERED_POSTFLUSH	= 0x20,
	QUEUE_ORDERED_FUA	= 0x40,

	QUEUE_ORDERED_DRAIN_FLUSH = QUEUE_ORDERED_DRAIN |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH,
	QUEUE_ORDERED_DRAIN_FUA	= QUEUE_ORDERED_DRAIN |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_FUA,
	QUEUE_ORDERED_TAG_FLUSH	= QUEUE_ORDERED_TAG |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH,
	QUEUE_ORDERED_TAG_FUA	= QUEUE_ORDERED_TAG |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_FUA,

	/*
	 * Ordered operation sequence
	 */
	QUEUE_ORDSEQ_STARTED	= 0x01,	/* flushing in progress */
	QUEUE_ORDSEQ_DRAIN	= 0x02,	/* waiting for the queue to be drained */
	QUEUE_ORDSEQ_PREFLUSH	= 0x04,	/* pre-flushing in progress */
	QUEUE_ORDSEQ_BAR	= 0x08,	/* original barrier req in progress */
	QUEUE_ORDSEQ_POSTFLUSH	= 0x10,	/* post-flushing in progress */
	QUEUE_ORDSEQ_DONE	= 0x20,
};

#define blk_queue_plugged(q)	test_bit(QUEUE_FLAG_PLUGGED, &(q)->queue_flags)
#define blk_queue_tagged(q)	test_bit(QUEUE_FLAG_QUEUED, &(q)->queue_flags)
#define blk_queue_stopped(q)	test_bit(QUEUE_FLAG_STOPPED, &(q)->queue_flags)
#define blk_queue_flushing(q)	((q)->ordseq)

#define blk_fs_request(rq)	((rq)->flags & REQ_CMD)
#define blk_pc_request(rq)	((rq)->flags & REQ_BLOCK_PC)
#define blk_noretry_request(rq)	((rq)->flags & REQ_FAILFAST)
#define blk_rq_started(rq)	((rq)->flags & REQ_STARTED)

#define blk_account_rq(rq)	(blk_rq_started(rq) && blk_fs_request(rq))

#define blk_pm_suspend_request(rq)	((rq)->flags & REQ_PM_SUSPEND)
#define blk_pm_resume_request(rq)	((rq)->flags & REQ_PM_RESUME)
#define blk_pm_request(rq)	\
	((rq)->flags & (REQ_PM_SUSPEND | REQ_PM_RESUME))

#define blk_sorted_rq(rq)	((rq)->flags & REQ_SORTED)
#define blk_barrier_rq(rq)	((rq)->flags & REQ_HARDBARRIER)
#define blk_fua_rq(rq)		((rq)->flags & REQ_FUA)

#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

#define rq_data_dir(rq)		((rq)->flags & 1)

static inline int blk_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		return test_bit(QUEUE_FLAG_READFULL, &q->queue_flags);
	return test_bit(QUEUE_FLAG_WRITEFULL, &q->queue_flags);
}

static inline void blk_set_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		set_bit(QUEUE_FLAG_READFULL, &q->queue_flags);
	else
		set_bit(QUEUE_FLAG_WRITEFULL, &q->queue_flags);
}

static inline void blk_clear_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		clear_bit(QUEUE_FLAG_READFULL, &q->queue_flags);
	else
		clear_bit(QUEUE_FLAG_WRITEFULL, &q->queue_flags);
}


/*
 * mergeable request must not have _NOMERGE or _BARRIER bit set, nor may
 * it already be started by driver.
 */
#define RQ_NOMERGE_FLAGS	\
	(REQ_NOMERGE | REQ_STARTED | REQ_HARDBARRIER | REQ_SOFTBARRIER)
#define rq_mergeable(rq)	\
	(!((rq)->flags & RQ_NOMERGE_FLAGS) && blk_fs_request((rq)))

/*
 * noop, requests are automagically marked as active/inactive by I/O
 * scheduler -- see elv_next_request
 */
#define blk_queue_headactive(q, head_active)

/*
 * q->prep_rq_fn return values
 */
#define BLKPREP_OK		0	/* serve it */
#define BLKPREP_KILL		1	/* fatal error, kill */
#define BLKPREP_DEFER		2	/* leave on queue */

extern unsigned long blk_max_low_pfn, blk_max_pfn;

/*
 * standard bounce addresses:
 *
 * BLK_BOUNCE_HIGH	: bounce all highmem pages
 * BLK_BOUNCE_ANY	: don't bounce anything
 * BLK_BOUNCE_ISA	: bounce pages above ISA DMA boundary
 */
#define BLK_BOUNCE_HIGH		((u64)blk_max_low_pfn << PAGE_SHIFT)
#define BLK_BOUNCE_ANY		((u64)blk_max_pfn << PAGE_SHIFT)
#define BLK_BOUNCE_ISA		(ISA_DMA_THRESHOLD)

#ifdef CONFIG_MMU
extern int init_emergency_isa_pool(void);
extern void blk_queue_bounce(request_queue_t *q, struct bio **bio);
#else
static inline int init_emergency_isa_pool(void)
{
	return 0;
}
static inline void blk_queue_bounce(request_queue_t *q, struct bio **bio)
{
}
#endif /* CONFIG_MMU */

#define rq_for_each_bio(_bio, rq)	\
	if ((rq->bio))			\
		for (_bio = (rq)->bio; _bio; _bio = _bio->bi_next)

struct sec_size {
	unsigned block_size;
	unsigned block_size_bits;
};

extern int blk_register_queue(struct gendisk *disk);
extern void blk_unregister_queue(struct gendisk *disk);
extern void register_disk(struct gendisk *dev);
extern void generic_make_request(struct bio *bio);
extern void blk_put_request(struct request *);
extern void __blk_put_request(request_queue_t *, struct request *);
extern void blk_end_sync_rq(struct request *rq, int error);
extern struct request *blk_get_request(request_queue_t *, int, gfp_t);
extern void blk_insert_request(request_queue_t *, struct request *, int, void *);
extern void blk_requeue_request(request_queue_t *, struct request *);
extern void blk_plug_device(request_queue_t *);
extern int blk_remove_plug(request_queue_t *);
extern void blk_recount_segments(request_queue_t *, struct bio *);
extern int scsi_cmd_ioctl(struct file *, struct gendisk *, unsigned int, void __user *);
extern int sg_scsi_ioctl(struct file *, struct request_queue *,
		struct gendisk *, struct scsi_ioctl_command __user *);
extern void blk_start_queue(request_queue_t *q);
extern void blk_stop_queue(request_queue_t *q);
extern void blk_sync_queue(struct request_queue *q);
extern void __blk_stop_queue(request_queue_t *q);
extern void blk_run_queue(request_queue_t *);
extern void blk_queue_activity_fn(request_queue_t *, activity_fn *, void *);
extern int blk_rq_map_user(request_queue_t *, struct request *, void __user *, unsigned int);
extern int blk_rq_unmap_user(struct bio *, unsigned int);
extern int blk_rq_map_kern(request_queue_t *, struct request *, void *, unsigned int, gfp_t);
extern int blk_rq_map_user_iov(request_queue_t *, struct request *, struct sg_iovec *, int);
extern int blk_execute_rq(request_queue_t *, struct gendisk *,
			  struct request *, int);
extern void blk_execute_rq_nowait(request_queue_t *, struct gendisk *,
				  struct request *, int, rq_end_io_fn *);

static inline request_queue_t *bdev_get_queue(struct block_device *bdev)
{
	return bdev->bd_disk->queue;
}

static inline void blk_run_backing_dev(struct backing_dev_info *bdi,
				       struct page *page)
{
	if (bdi && bdi->unplug_io_fn)
		bdi->unplug_io_fn(bdi, page);
}

static inline void blk_run_address_space(struct address_space *mapping)
{
	if (mapping)
		blk_run_backing_dev(mapping->backing_dev_info, NULL);
}

/*
 * end_request() and friends. Must be called with the request queue spinlock
 * acquired. All functions called within end_request() _must_be_ atomic.
 *
 * Several drivers define their own end_request and call
 * end_that_request_first() and end_that_request_last()
 * for parts of the original function. This prevents
 * code duplication in drivers.
 */
extern int end_that_request_first(struct request *, int, int);
extern int end_that_request_chunk(struct request *, int, int);
extern void end_that_request_last(struct request *, int);
extern void end_request(struct request *req, int uptodate);
extern void blk_complete_request(struct request *);

static inline int rq_all_done(struct request *rq, unsigned int nr_bytes)
{
	if (blk_fs_request(rq))
		return (nr_bytes >= (rq->hard_nr_sectors << 9));
	else if (blk_pc_request(rq))
		return nr_bytes >= rq->data_len;

	return 0;
}

/*
 * end_that_request_first/chunk() takes an uptodate argument. we account
 * any value <= as an io error. 0 means -EIO for compatability reasons,
 * any other < 0 value is the direct error type. An uptodate value of
 * 1 indicates successful io completion
 */
#define end_io_error(uptodate)	(unlikely((uptodate) <= 0))

static inline void blkdev_dequeue_request(struct request *req)
{
	elv_dequeue_request(req->q, req);
}

/*
 * This should be in elevator.h, but that requires pulling in rq and q
 */
static inline void elv_dispatch_add_tail(struct request_queue *q,
					 struct request *rq)
{
	if (q->last_merge == rq)
		q->last_merge = NULL;
	q->nr_sorted--;

	q->end_sector = rq_end_sector(rq);
	q->boundary_rq = rq;
	list_add_tail(&rq->queuelist, &q->queue_head);
}

/*
 * Access functions for manipulating queue properties
 */
extern request_queue_t *blk_init_queue_node(request_fn_proc *rfn,
					spinlock_t *lock, int node_id);
extern request_queue_t *blk_init_queue(request_fn_proc *, spinlock_t *);
extern void blk_cleanup_queue(request_queue_t *);
extern void blk_queue_make_request(request_queue_t *, make_request_fn *);
extern void blk_queue_bounce_limit(request_queue_t *, u64);
extern void blk_queue_max_sectors(request_queue_t *, unsigned int);
extern void blk_queue_max_phys_segments(request_queue_t *, unsigned short);
extern void blk_queue_max_hw_segments(request_queue_t *, unsigned short);
extern void blk_queue_max_segment_size(request_queue_t *, unsigned int);
extern void blk_queue_hardsect_size(request_queue_t *, unsigned short);
extern void blk_queue_stack_limits(request_queue_t *t, request_queue_t *b);
extern void blk_queue_segment_boundary(request_queue_t *, unsigned long);
extern void blk_queue_prep_rq(request_queue_t *, prep_rq_fn *pfn);
extern void blk_queue_merge_bvec(request_queue_t *, merge_bvec_fn *);
extern void blk_queue_dma_alignment(request_queue_t *, int);
extern void blk_queue_softirq_done(request_queue_t *, softirq_done_fn *);
extern struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev);
extern int blk_queue_ordered(request_queue_t *, unsigned, prepare_flush_fn *);
extern void blk_queue_issue_flush_fn(request_queue_t *, issue_flush_fn *);
extern int blk_do_ordered(request_queue_t *, struct request **);
extern unsigned blk_ordered_cur_seq(request_queue_t *);
extern unsigned blk_ordered_req_seq(struct request *);
extern void blk_ordered_complete_seq(request_queue_t *, unsigned, int);

extern int blk_rq_map_sg(request_queue_t *, struct request *, struct scatterlist *);
extern void blk_dump_rq_flags(struct request *, char *);
extern void generic_unplug_device(request_queue_t *);
extern void __generic_unplug_device(request_queue_t *);
extern long nr_blockdev_pages(void);

int blk_get_queue(request_queue_t *);
request_queue_t *blk_alloc_queue(gfp_t);
request_queue_t *blk_alloc_queue_node(gfp_t, int);
extern void blk_put_queue(request_queue_t *);

/*
 * tag stuff
 */
#define blk_queue_tag_depth(q)		((q)->queue_tags->busy)
#define blk_queue_tag_queue(q)		((q)->queue_tags->busy < (q)->queue_tags->max_depth)
#define blk_rq_tagged(rq)		((rq)->flags & REQ_QUEUED)
extern int blk_queue_start_tag(request_queue_t *, struct request *);
extern struct request *blk_queue_find_tag(request_queue_t *, int);
extern void blk_queue_end_tag(request_queue_t *, struct request *);
extern int blk_queue_init_tags(request_queue_t *, int, struct blk_queue_tag *);
extern void blk_queue_free_tags(request_queue_t *);
extern int blk_queue_resize_tags(request_queue_t *, int);
extern void blk_queue_invalidate_tags(request_queue_t *);
extern long blk_congestion_wait(int rw, long timeout);

extern void blk_rq_bio_prep(request_queue_t *, struct request *, struct bio *);
extern int blkdev_issue_flush(struct block_device *, sector_t *);

#define MAX_PHYS_SEGMENTS 128
#define MAX_HW_SEGMENTS 128
#define SAFE_MAX_SECTORS 255
#define BLK_DEF_MAX_SECTORS 1024

#define MAX_SEGMENT_SIZE	65536

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)

static inline int queue_hardsect_size(request_queue_t *q)
{
	int retval = 512;

	if (q && q->hardsect_size)
		retval = q->hardsect_size;

	return retval;
}

static inline int bdev_hardsect_size(struct block_device *bdev)
{
	return queue_hardsect_size(bdev_get_queue(bdev));
}

static inline int queue_dma_alignment(request_queue_t *q)
{
	int retval = 511;

	if (q && q->dma_alignment)
		retval = q->dma_alignment;

	return retval;
}

static inline int bdev_dma_aligment(struct block_device *bdev)
{
	return queue_dma_alignment(bdev_get_queue(bdev));
}

#define blk_finished_io(nsects)	do { } while (0)
#define blk_started_io(nsects)	do { } while (0)

/* assumes size > 256 */
//size一般都不小于512，假设size > 256，这样可以减少8次位移
static inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do {
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

static inline unsigned int block_size(struct block_device *bdev)
{
	return bdev->bd_block_size;
}

typedef struct {struct page *v;} Sector;

unsigned char *read_dev_sector(struct block_device *, sector_t, Sector *);

static inline void put_dev_sector(Sector p)
{
	page_cache_release(p.v);
}

struct work_struct;
int kblockd_schedule_work(struct work_struct *work);
void kblockd_flush(void);

#ifdef CONFIG_LBD
# include <asm/div64.h>
# define sector_div(a, b) do_div(a, b)
#else
# define sector_div(n, b)( \
{ \
	int _res; \
	_res = (n) % (b); \
	(n) /= (b); \
	_res; \
} \
)
#endif 

#define MODULE_ALIAS_BLOCKDEV(major,minor) \
	MODULE_ALIAS("block-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_BLOCKDEV_MAJOR(major) \
	MODULE_ALIAS("block-major-" __stringify(major) "-*")


#endif
