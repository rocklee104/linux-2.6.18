/*
 *  linux/drivers/base/map.c
 *
 * (C) Copyright Al Viro 2002,2003
 *	Released under GPL v2.
 *
 * NOTE: data structure needs to be changed.  It works, but for large dev_t
 * it will be too slow.  It is isolated, though, so these changes will be
 * local to that file.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/kobj_map.h>

struct kobj_map {
	//散列表元素,通过单链表组织
	struct probe {
		//将散列元素连接在一个单链表中
		struct probe *next;
		dev_t dev;
		//设备号的连续范围, 链表中range从小到大排列
		unsigned long range;
		//指向提供设备驱动的模块
		struct module *owner;
		//指向一个函数,返回与设备关联的kobject实例
		kobj_probe_t *get;
		int (*lock)(dev_t, void *);
		//对于字符设备,data指向struct cdev的一个实例
		//对于block device, data指向struct genhd的实例
		void *data;
	} *probes[255];
	struct mutex *lock;
};

int kobj_map(struct kobj_map *domain, dev_t dev, unsigned long range,
	     struct module *module, kobj_probe_t *probe,
	     int (*lock)(dev_t, void *), void *data)
{
	//主设备的个数
	unsigned n = MAJOR(dev + range - 1) - MAJOR(dev) + 1;
	unsigned index = MAJOR(dev);
	unsigned i;
	struct probe *p;

	if (n > 255)
		n = 255;

	p = kmalloc(sizeof(struct probe) * n, GFP_KERNEL);

	if (p == NULL)
		return -ENOMEM;

	for (i = 0; i < n; i++, p++) {
		p->owner = module;
		p->get = probe;
		p->lock = lock;
		p->dev = dev;
		p->range = range;
		p->data = data;
	}
	mutex_lock(domain->lock);
	for (i = 0, p -= n; i < n; i++, p++, index++) {
		struct probe **s = &domain->probes[index % 255];
		while (*s && (*s)->range < range)
			s = &(*s)->next;

		//将p查到s前面,保证range从小到大排列
		p->next = *s;
		*s = p;
	}
	mutex_unlock(domain->lock);
	return 0;
}

void kobj_unmap(struct kobj_map *domain, dev_t dev, unsigned long range)
{
	unsigned n = MAJOR(dev + range - 1) - MAJOR(dev) + 1;
	unsigned index = MAJOR(dev);
	unsigned i;
	struct probe *found = NULL;

	if (n > 255)
		n = 255;

	mutex_lock(domain->lock);
	for (i = 0; i < n; i++, index++) {
		struct probe **s;
		for (s = &domain->probes[index % 255]; *s; s = &(*s)->next) {
			struct probe *p = *s;
			if (p->dev == dev && p->range == range) {
				*s = p->next;
				if (!found)
					found = p;
				break;
			}
		}
	}
	mutex_unlock(domain->lock);
	kfree(found);
}
 
//通过设备号获取kobject指针
struct kobject *kobj_lookup(struct kobj_map *domain, dev_t dev, int *index)
{
	struct kobject *kobj;
	struct probe *p;
	unsigned long best = ~0UL;

retry:
	mutex_lock(domain->lock);
	for (p = domain->probes[MAJOR(dev) % 255]; p; p = p->next) {
		struct kobject *(*probe)(dev_t, int *, void *);
		struct module *owner;
		void *data;

		//链表中的元素, range从小到大排列, p->dev应该要<=dev,
		//p->dev + p->range - 1也就是最大的一个次设备号成员应该>=dev,
		//初始化后p->dev就是主设备号, p->range为(-1)UL
		if (p->dev > dev || p->dev + p->range - 1 < dev)
			continue;

		//p->range最大为~0, 故p->range - 1必须要<best
		if (p->range - 1 >= best)
			break;
		
		if (!try_module_get(p->owner))
			//模块获取失败
			continue;
		
		owner = p->owner;
		data = p->data;
		probe = p->get;
		best = p->range - 1;
		//获取次设备号
		*index = dev - p->dev;
		if (p->lock && p->lock(dev, data) < 0) {
			module_put(owner);
			continue;
		}
		mutex_unlock(domain->lock);
		kobj = probe(dev, index, data);
		/* Currently ->owner protects _only_ ->probe() itself. */
		module_put(owner);
		if (kobj)
			return kobj;
		goto retry;
	}
	mutex_unlock(domain->lock);
	return NULL;
}

struct kobj_map *kobj_map_init(kobj_probe_t *base_probe, struct mutex *lock)
{
	struct kobj_map *p = kmalloc(sizeof(struct kobj_map), GFP_KERNEL);
	struct probe *base = kzalloc(sizeof(*base), GFP_KERNEL);
	int i;

	if ((p == NULL) || (base == NULL)) {
		kfree(p);
		kfree(base);
		return NULL;
	}

	base->dev = 1;
	base->range = ~0;
	base->get = base_probe;
	//给backet初始化
	for (i = 0; i < 255; i++)
		p->probes[i] = base;
	p->lock = lock;
	return p;
}
