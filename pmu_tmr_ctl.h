/* SPDX-License-Identifier: (BSD-3-Clause OR LGPL-2.1) */
#ifndef _PMU_TMR_CTL_H
#define _PMU_TMR_CTL_H

/* extern declarations for timer control handers */
extern void pm_cntkctl_handler(int enable);
extern void pm_cntkctl_fini(void);
extern ssize_t pmcntkctl_show(char *arg, size_t size);
extern int pmcntkctl_modify(const char *arg, size_t size);

#endif
