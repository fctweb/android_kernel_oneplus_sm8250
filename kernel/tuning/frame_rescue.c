/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Frame Boost Lite Rescue — SM8850 cfbt_rescue 403L simplified
 * No timer_data[16] / 8-sample averaging. Logic lives in frame_group.c
 */

#include <linux/sched.h>
#include <linux/seq_file.h>
#include "frame_rescue.h"

int sysctl_rescue_enable = 1;
int sysctl_rescue_stage_enhance = 512;
int sysctl_rescue_frame_enhance = 512;

void frame_rescue_init(void)
{
	pr_info("[frame_rescue] Lite init enable=%d stage=%d frame=%d\n",
		sysctl_rescue_enable, sysctl_rescue_stage_enhance,
		sysctl_rescue_frame_enhance);
}

int frame_rescue_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "enable=%d\nstage_enhance=%d\nframe_enhance=%d\n",
		 sysctl_rescue_enable, sysctl_rescue_stage_enhance,
		 sysctl_rescue_frame_enhance);
	return 0;
}
