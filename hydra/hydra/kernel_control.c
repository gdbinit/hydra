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
 * kernel_control.c
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

#include "kernel_control.h"

#include <sys/conf.h>
#include <sys/kernel.h>
#include <string.h>
#include <sys/systm.h>
#include <stdbool.h>
#include <sys/param.h>
#include <stdint.h>
#include <sys/kern_control.h>

#include "shared_data.h"
#include "my_data_definitions.h"

// local functions
static int ctl_connect(kern_ctl_ref ctl_ref, struct sockaddr_ctl *sac, void **unitinfo);
static errno_t ctl_disconnect(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo);
static int ctl_get(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt, void *data, size_t *len);
static int ctl_set(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt, void *data, size_t len);

// vars, external and local
extern targets_t g_targets_list;

static boolean_t gKernCtlRegistered = FALSE;
static int max_clients;
static uint32_t gClientUnit = 0;
static kern_ctl_ref gClientCtlRef = NULL;
static kern_ctl_ref gctl_ref;

#pragma mark Kernel Control struct and handler functions

// described at Network Kernel Extensions Programming Guide
static struct kern_ctl_reg gctl_reg = {
	BUNDLE_ID,              /* use a reverse dns name which includes a name unique to your comany */
	0,						/* set to 0 for dynamically assigned control ID - CTL_FLAG_REG_ID_UNIT not set */
	0,						/* ctl_unit - ignored when CTL_FLAG_REG_ID_UNIT not set */
	CTL_FLAG_PRIVILEGED,	/* privileged access required to access this filter */
	0,						/* use default send size buffer */
	0,                      /* Override receive buffer size */
	ctl_connect,			/* Called when a connection request is accepted */
	ctl_disconnect,			/* called when a connection becomes disconnected */
	NULL,					/* ctl_send_func - handles data sent from the client to kernel control */
	ctl_set,				/* called when the user process makes the setsockopt call */
	ctl_get					/* called when the user process makes the getsockopt call */
};

#pragma mark start and stop functions, the only exported ones

/*
 * initialize the kernel control
 */
kern_return_t
start_kern_control(void)
{
    errno_t error = 0;
    // register the kernel control
    error = ctl_register(&gctl_reg, &gctl_ref);
    if (error == 0)
    {
        gKernCtlRegistered = TRUE;
        return KERN_SUCCESS;
    }
    else
    {
        LOG_MSG("[ERROR] Could not initialize control channel!\n");
        return KERN_FAILURE;
    }
}

/*
 * and remove it
 */
kern_return_t
stop_kern_control(void)
{
    errno_t error = 0;
    // remove kernel control
    // XXX: this is useless since we fail if control failed to install
    if (gKernCtlRegistered == TRUE)
    {
        error = ctl_deregister(gctl_ref);
    }
    return KERN_SUCCESS;
}

/*
 * get data ready for userland to grab
 * we only send PID of the suspended process and let the userland daemon do the rest
 */
kern_return_t
queue_userland_data(pid_t pid)
{
    errno_t error = 0;
    
    if (gClientCtlRef == NULL)
    {
        LOG_MSG("[ERROR] No client reference available, can't proceed...\n");
        return KERN_FAILURE;
    }
    
    error = ctl_enqueuedata(gClientCtlRef, gClientUnit, &pid, sizeof(pid_t), 0);
    if (error)
    {
        LOG_MSG("[ERROR] ctl_enqueuedata failed with error: %d\n", error);
        return KERN_FAILURE;
    }
    
    return KERN_SUCCESS;
}

#pragma mark Kernel Control handler functions

/*
 * called when a client connects to the socket
 * we need to store some info to use later
 */
static int
ctl_connect(kern_ctl_ref ctl_ref, struct sockaddr_ctl *sac, void **unitinfo)
{
    // we only accept a single client
    if (max_clients > 0)
    {
        LOG_MSG("[ERROR] Maximum number of clients reached!\n");
        return EBUSY;
    }
    max_clients++;
    // store the unit id and ctl_ref of the client that connected
    // we will need these to queue data to userland
    gClientUnit = sac->sc_unit;
    gClientCtlRef = ctl_ref;
    return 0;
}

/*
 * and when client disconnects
 */
static errno_t
ctl_disconnect(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo)
{
    // reset max clients
    max_clients = 0;
    gClientUnit = 0;
    gClientCtlRef = NULL;
    return 0;
}

/*
 * kernel -> userland
 */
static int
ctl_get(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt, void *data, size_t *len)
{
    int		error = 0;
	size_t  valsize;
	void    *buf = NULL;
	switch (opt)
    {
        case 0:
        {
            valsize = 0;
            break;
        }
        default:
            error = ENOTSUP;
            break;
    }
    if (error == 0)
    {
        *len = valsize;
        if (data != NULL) bcopy(buf, data, valsize);
    }
    return error;
}

/*
 * userland -> kernel
 * this is how userland apps adds and removes apps to be suspended
 */
static int
ctl_set(kern_ctl_ref ctl_ref, u_int32_t unit, void *unitinfo, int opt, void *data, size_t len)
{
    int error = 0;

	switch (opt)
	{
        case ADD_APP:
        {
            if (len > 0 && data != NULL)
            {
                targets_t temp;
                HASH_FIND_STR(g_targets_list, (char*)data, temp);
                if (temp == NULL)
                {
                    temp = _MALLOC(sizeof(struct targets), 1, M_ZERO);
                    if (temp != NULL)
                    {
                        // the process name will be truncated at MAXCOMLEN so there's no need to allocate more than that
                        if (len >= MAXCOMLEN)
                        {
                            temp->name = _MALLOC(MAXCOMLEN+1, 1, M_ZERO);
                            len = MAXCOMLEN+1;
                        }
                        else
                        {
                            // the len should include the space for the nul char
                            // else we will lose last char
                            temp->name = _MALLOC(len, 1, M_ZERO);
                        }
                        strlcpy(temp->name, (const char*)data, len);
                        // we need to recompute len or substract 1 else hash will not match strings
                        HASH_ADD_KEYPTR(hh, g_targets_list, temp->name, (int)strlen(temp->name), temp);
                    }
                }
            }
            break;
        }
        case REMOVE_APP:
        {
            if (len > 0 && data != NULL)
            {
                targets_t temp;
                HASH_FIND_STR(g_targets_list, (char*)data, temp);
                if (temp)
                {
#if DEBUG
                    LOG_MSG("[DEBUG] Found element in list, removing!\n");
#endif
                    // free the name string we alloc'ed before
                    _FREE(temp->name, M_ZERO);
                    // remove from the list and free the element
                    HASH_DEL(g_targets_list, temp);
                    _FREE(temp, M_ZERO);
                }
            }
            break;
        }
        default:
            error = ENOTSUP;
            break;
    }
    return error;
}
