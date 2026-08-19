/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SA Pipeline Lite — simplified from SM8850 sa_pipeline.c (1312L)
 * Max 6 tasks, prime -> nr_cpu_ids-1, uses task_struct.pipeline_cpu
 */

#ifndef _SA_PIPELINE_LITE_H
#define _SA_PIPELINE_LITE_H

#include <linux/sched.h>

#define MAX_PIPELINE_TASK_NUM 6

struct task_struct;

int oplus_get_task_pipeline_cpu(struct task_struct *task);
struct task_struct *oplus_pipeline_get_prime_task(void);
bool oplus_pipeline_task_skip_cpu(struct task_struct *task, unsigned int dst_cpu);
bool oplus_is_pipeline_scene(void);
void sa_pipeline_lite_init(void);
void qcom_rearrange_pipeline_preferred_cpus(unsigned int divisor);

#endif
