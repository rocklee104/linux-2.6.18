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
	//ɢ�б�Ԫ��,ͨ����������֯
	struct probe {
		//��ɢ��Ԫ��������һ����������
		struct probe *next;
		dev_t dev;
		//�豸�ŵ�������Χ, ������range��С��������
		unsigned long range;
		//ָ���ṩ�豸������ģ��
		struct module *owner;
		//ָ��һ������,�������豸������kobjectʵ��
		kobj_probe_t *get;
		int (*lock)(dev_t, void *);
		//�����ַ��豸,dataָ��struct cdev��һ��ʵ��
		//����block device, dataָ��struct genhd��ʵ��
		void *data;
	} *probes[255];
	struct mutex *lock;
};

int kobj_map(struct kobj_map *domain, dev_t dev, unsigned long range,
	     struct module *module, kobj_probe_t *probe,
	     int (*lock)(dev_t, void *), void *data)
{
	//���豸�ĸ���
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

		//��p�鵽sǰ��,��֤range��С��������
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
 
//ͨ���豸�Ż�ȡkobjectָ��
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

		//�����е�Ԫ��, range��С��������, p->devӦ��Ҫ<=dev,
		//p->dev + p->range - 1Ҳ��������һ�����豸�ų�ԱӦ��>=dev,
		//��ʼ����p->dev�������豸��, p->rangeΪ(-1)UL
		if (p->dev > dev || p->dev + p->range - 1 < dev)
			continue;

		//p->range���Ϊ~0, ��p->range - 1����Ҫ<best
		if (p->range - 1 >= best)
			break;
		
		if (!try_module_get(p->owner))
			//ģ���ȡʧ��
			continue;
		
		owner = p->owner;
		data = p->data;
		probe = p->get;
		best = p->range - 1;
		//��ȡ���豸��
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
	//��backet��ʼ��
	for (i = 0; i < 255; i++)
		p->probes[i] = base;
	p->lock = lock;
	return p;
}
