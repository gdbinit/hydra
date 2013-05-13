/*
 *
 *                                                        dddddddd
 * HHHHHHHHH     HHHHHHHHH                                d::::::d
 * H:::::::H     H:::::::H                                d::::::d
 * H:::::::H     H:::::::H                                d::::::d
 * HH::::::H     H::::::HH                                d:::::d
 *   H:::::H     H:::::Hyyyyyyy           yyyyyyy ddddddddd:::::drrrrr   rrrrrrrrr   aaaaaaaaaaaaa
 *   H:::::H     H:::::H y:::::y         y:::::ydd::::::::::::::dr::::rrr:::::::::r  a::::::::::::a
 *   H::::::HHHHH::::::H  y:::::y       y:::::yd::::::::::::::::dr:::::::::::::::::r aaaaaaaaa:::::a
 *   H:::::::::::::::::H   y:::::y     y:::::yd:::::::ddddd:::::drr::::::rrrrr::::::r         a::::a
 *   H:::::::::::::::::H    y:::::y   y:::::y d::::::d    d:::::d r:::::r     r:::::r  aaaaaaa:::::a
 *   H::::::HHHHH::::::H     y:::::y y:::::y  d:::::d     d:::::d r:::::r     rrrrrrraa::::::::::::a
 *   H:::::H     H:::::H      y:::::y:::::y   d:::::d     d:::::d r:::::r           a::::aaaa::::::a
 *   H:::::H     H:::::H       y:::::::::y    d:::::d     d:::::d r:::::r          a::::a    a:::::a
 * HH::::::H     H::::::HH      y:::::::y     d::::::ddddd::::::ddr:::::r          a::::a    a:::::a
 * H:::::::H     H:::::::H       y:::::y       d:::::::::::::::::dr:::::r          a:::::aaaa::::::a
 * H:::::::H     H:::::::H      y:::::y         d:::::::::ddd::::dr:::::r           a::::::::::aa:::a
 * HHHHHHHHH     HHHHHHHHH     y:::::y           ddddddddd   dddddrrrrrrr            aaaaaaaaaa  aaaa
 *                            y:::::y
 *                           y:::::y
 *                          y:::::y
 *                         y:::::y
 *                        yyyyyyy
 *
 * A kernel extension to suspend applications and notify a userland daemon
 *
 * Copyright (c) 2012,2013 fG!. All rights reserved.
 * reverser@put.as - http://reverse.put.as
 *
 * suspend_proc.c
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "suspend_proc.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/vnode.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/queue.h>

#include "kernel_info.h"
#include "kernel_control.h"

/* Status values. */
#define	SIDL	1		/* Process being created by fork. */
#define	SRUN	2		/* Currently runnable. */
#define	SSLEEP	3		/* Sleeping on an address. */
#define	SSTOP	4		/* Process debugging or suspension. */
#define	SZOMB	5		/* Awaiting collection by parent. */

/* other definitions we need to import */
#define P_LREGISTER	0x00800000	/* thread start fns registered  */
#define proc_lock(p)		lck_mtx_lock(&(p)->p_mlock)
#define proc_unlock(p)      lck_mtx_unlock(&(p)->p_mlock)

extern struct kernel_info g_kernel_info;
extern targets_t g_targets_list;

kern_return_t (*_task_suspend)(task_t target_task);

/*
 * function to replace the original proc_resetregister and suspend the processes we are interested in
 */
void
myproc_resetregister(proc_t p)
{
    // this symbol is not exported so we need to solve it first
    if (_task_suspend == NULL)
    {
        _task_suspend = (void*)(solve_kernel_symbol(&g_kernel_info, "_task_suspend"));
        // if we can't solve the symbol then get back to the original code
        if (_task_suspend == NULL)
        {
            LOG_MSG("[ERROR] Failed to solve task_suspend() symbol...\n");
            goto original_code;
        }
    }
    // activate proc_t lock to avoid problems
    proc_lock(p);
    // retrieve the name of the new process being executed
    pid_t pid = p->p_pid;
    char processname[MAXCOMLEN+1];
    proc_name(p->p_pid, processname, sizeof(processname));
    // try to find it on our targets list
    targets_t temp = { 0 };
    HASH_FIND_STR(g_targets_list, processname, temp);
    proc_unlock(p);
    // found something
    if (temp)
    {
        /*
         * If posix_spawned with the START_SUSPENDED flag, stop the
         * process before it runs.
         */
        /*
         if (imgp->ip_px_sa != NULL) {
         psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
         if (psa->psa_flags & POSIX_SPAWN_START_SUSPENDED) {
         proc_lock(p);
         p->p_stat = SSTOP;
         proc_unlock(p);
         (void) task_suspend(p->task);
         }
         */
        proc_lock(p);
        p->p_stat = SSTOP;
        proc_unlock(p);
        if (_task_suspend(p->task) == KERN_SUCCESS)
        {
            // queue data for userland process
            queue_userland_data(pid);
        }
    }
    // the original function code
original_code:
	proc_lock(p);
	p->p_lflag &= ~P_LREGISTER;
	proc_unlock(p);
}
