/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Frame Boost Lite Rescue — simplified from SM8850 CFBT cfbt_rescue
 * Per-group deadline (~60% window, ~5ms@120Hz) + enhance 512 (1.5x)
 */

#ifndef _FRAME_RESCUE_H
#define _FRAME_RESCUE_H

#include <linux/seq_file.h>

#define RESCUE_OF_STAGE		(1 << 0)
#define RESCUE_OF_FRAME		(1 << 1)

extern int sysctl_rescue_enable;
extern int sysctl_rescue_stage_enhance;
extern int sysctl_rescue_frame_enhance;

void frame_rescue_init(void);
int frame_rescue_proc_show(struct seq_file *m, void *v);

#endif /* _FRAME_RESCUE_H */
