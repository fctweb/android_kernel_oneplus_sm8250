/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SA Pipeline Lite — SM8850 sa_pipeline.c simplified for SM8250 4.19
 * Keeps ABI: prime CPU affinity + pipeline util policy, no vendor
 * oplus_task_struct dependency. Uses task_struct.pipeline_cpu.
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <../kernel/sched/sched.h>
extern int sysctl_sched_assist_enabled;
#include "sched_assist_common.h"
#include "sa_pipeline_lite.h"

#define PIPELINE_TASK_UX_STATE (UX_PRIORITY_PIPELINE | SA_TYPE_HEAVY)
#define PIPELINE_UI_TASK_UX_STATE (UX_PRIORITY_PIPELINE_UI | SA_TYPE_LIGHT)

static int pipeline_pids[MAX_PIPELINE_TASK_NUM] = { -1, -1, -1, -1, -1, -1 };
static int pipeline_cpus[MAX_PIPELINE_TASK_NUM] = { -1, -1, -1, -1, -1, -1 };
static struct task_struct *pipeline_task[MAX_PIPELINE_TASK_NUM] = { NULL };
static struct task_struct *prime_task;
static pid_t prime_tgid;
static unsigned int pipeline_task_nr;
static DEFINE_RAW_SPINLOCK(pipeline_lock);

static inline bool is_valid_pipeline_task(int pipeline_cpu, int ux_state)
{
	return (pipeline_cpu >= 0) && (pipeline_cpu < nr_cpu_ids) &&
	       (((ux_state & PIPELINE_TASK_UX_STATE) == PIPELINE_TASK_UX_STATE) ||
		((ux_state & PIPELINE_UI_TASK_UX_STATE) == PIPELINE_UI_TASK_UX_STATE));
}

int oplus_get_task_pipeline_cpu(struct task_struct *task)
{
	int cpu;
	if (!task)
		return -1;
	cpu = READ_ONCE(task->pipeline_cpu);
	if (cpu < 0 || cpu >= nr_cpu_ids)
		return -1;
	if (is_valid_pipeline_task(cpu, READ_ONCE(task->ux_state)))
		return cpu;
	return -1;
}
EXPORT_SYMBOL_GPL(oplus_get_task_pipeline_cpu);

struct task_struct *oplus_pipeline_get_prime_task(void)
{
	return READ_ONCE(prime_task);
}
EXPORT_SYMBOL_GPL(oplus_pipeline_get_prime_task);

bool oplus_is_pipeline_scene(void)
{
	return READ_ONCE(pipeline_task_nr) >= 1;
}
EXPORT_SYMBOL_GPL(oplus_is_pipeline_scene);

bool oplus_pipeline_task_skip_cpu(struct task_struct *task, unsigned int dst_cpu)
{
	struct task_struct *prime;
	int pcpu;

	if (!sysctl_sched_assist_enabled)
		return false;

	prime = READ_ONCE(prime_task);
	if (!prime)
		return false;

	/* keep prime on prime CPU(s) near nr_cpu_ids-1 */
	if (task == prime && dst_cpu < (unsigned int)(nr_cpu_ids - 1) && nr_cpu_ids > 2)
		return true;

	/* non-pipeline should avoid prime CPU if pipeline scene active */
	pcpu = oplus_get_task_pipeline_cpu(task);
	if (pcpu < 0 && dst_cpu == (unsigned int)(nr_cpu_ids - 1) && oplus_is_pipeline_scene()) {
		/* allow if dest is not heavily loaded; lite keeps it simple */
		if (cpu_rq(dst_cpu)->nr_running > 1)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(oplus_pipeline_task_skip_cpu);

static void pipeline_set_locked(int idx, struct task_struct *tsk, int cpu)
{
	if (pipeline_task[idx]) {
		put_task_struct(pipeline_task[idx]);
		pipeline_task[idx] = NULL;
	}
	if (tsk) {
		get_task_struct(tsk);
		pipeline_task[idx] = tsk;
		pipeline_pids[idx] = tsk->pid;
		pipeline_cpus[idx] = cpu;
		WRITE_ONCE(tsk->pipeline_cpu, cpu);
		/* mark as pipeline ux if not already */
		if ((tsk->ux_state & SCHED_ASSIST_UX_PRIORITY_MASK) != UX_PRIORITY_PIPELINE &&
		    (tsk->ux_state & SCHED_ASSIST_UX_PRIORITY_MASK) != UX_PRIORITY_PIPELINE_UI) {
			tsk->ux_state = (tsk->ux_state & ~SCHED_ASSIST_UX_PRIORITY_MASK) | UX_PRIORITY_PIPELINE;
			tsk->ux_state |= SA_TYPE_HEAVY;
		}
	} else {
		pipeline_pids[idx] = -1;
		pipeline_cpus[idx] = -1;
	}
}

static ssize_t pipeline_proc_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	char kbuf[256];
	char *cur, *tok;
	int idx = 0;
	unsigned long flags;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	cur = strstrip(kbuf);

	/* format: "pid:cpu pid:cpu ..." or "clear" */
	if (strncmp(cur, "clear", 5) == 0) {
		raw_spin_lock_irqsave(&pipeline_lock, flags);
		for (idx = 0; idx < MAX_PIPELINE_TASK_NUM; idx++) {
			if (pipeline_task[idx])
				WRITE_ONCE(pipeline_task[idx]->pipeline_cpu, -1);
			pipeline_set_locked(idx, NULL, -1);
		}
		pipeline_task_nr = 0;
		prime_task = NULL;
		prime_tgid = 0;
		raw_spin_unlock_irqrestore(&pipeline_lock, flags);
		return count;
	}

	raw_spin_lock_irqsave(&pipeline_lock, flags);
	/* clear previous */
	for (idx = 0; idx < MAX_PIPELINE_TASK_NUM; idx++) {
		if (pipeline_task[idx])
			WRITE_ONCE(pipeline_task[idx]->pipeline_cpu, -1);
		pipeline_set_locked(idx, NULL, -1);
	}
	pipeline_task_nr = 0;
	prime_task = NULL;

	idx = 0;
	while ((tok = strsep(&cur, " ")) != NULL) {
		int pid, cpu;
		struct task_struct *tsk;
		if (!*tok)
			continue;
		if (sscanf(tok, "%d:%d", &pid, &cpu) != 2)
			continue;
		if (idx >= MAX_PIPELINE_TASK_NUM)
			break;
		if (cpu < 0 || cpu >= nr_cpu_ids)
			cpu = nr_cpu_ids - 1;
		rcu_read_lock();
		tsk = find_task_by_vpid(pid);
		if (tsk)
			get_task_struct(tsk);
		rcu_read_unlock();
		if (!tsk)
			continue;
		pipeline_set_locked(idx, tsk, cpu);
		if (cpu == nr_cpu_ids - 1 && !prime_task) {
			prime_task = tsk;
			prime_tgid = tsk->tgid;
			get_task_struct(prime_task);
		}
		put_task_struct(tsk);
		idx++;
		pipeline_task_nr++;
	}
	/* fallback prime to first task if none on last cpu */
	if (!prime_task && pipeline_task[0]) {
		prime_task = pipeline_task[0];
		prime_tgid = prime_task->tgid;
		get_task_struct(prime_task);
		WRITE_ONCE(prime_task->pipeline_cpu, nr_cpu_ids - 1);
		pipeline_cpus[0] = nr_cpu_ids - 1;
	}
	raw_spin_unlock_irqrestore(&pipeline_lock, flags);

	pr_info("[pipeline_lite] set nr=%u prime=%s pid=%d cpu=%d\n",
		pipeline_task_nr, prime_task ? prime_task->comm : "none",
		prime_task ? prime_task->pid : -1,
		prime_task ? READ_ONCE(prime_task->pipeline_cpu) : -1);
	return count;
}

static int pipeline_proc_show(struct seq_file *m, void *v)
{
	int i;
	unsigned long flags;
	raw_spin_lock_irqsave(&pipeline_lock, flags);
	seq_printf(m, "nr=%u prime=%s pid=%d\n", pipeline_task_nr,
		   prime_task ? prime_task->comm : "none",
		   prime_task ? prime_task->pid : -1);
	for (i = 0; i < MAX_PIPELINE_TASK_NUM; i++) {
		if (pipeline_task[i])
			seq_printf(m, "%d:%d %s pipeline_cpu=%d ux=0x%x\n",
				   pipeline_pids[i], pipeline_cpus[i],
				   pipeline_task[i]->comm,
				   READ_ONCE(pipeline_task[i]->pipeline_cpu),
				   READ_ONCE(pipeline_task[i]->ux_state));
	}
	raw_spin_unlock_irqrestore(&pipeline_lock, flags);
	return 0;
}

static int pipeline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pipeline_proc_show, NULL);
}

static const struct file_operations pipeline_fops = {
	.open = pipeline_proc_open,
	.read = seq_read,
	.write = pipeline_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void sa_pipeline_lite_init(void)
{
	if (!proc_create("pipeline_lite", 0666, NULL, &pipeline_fops))
		pr_err("[pipeline_lite] failed to create proc\n");
	else
		pr_info("[pipeline_lite] init nr_cpu_ids=%d\n", nr_cpu_ids);
}
