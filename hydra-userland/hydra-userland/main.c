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
 * The userland daemon to talk to the kernel and process the target apps
 *
 * Copyright (c) 2012,2013 fG!. All rights reserved.
 * reverser@put.as - http://reverse.put.as
 *
 * main.c
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/kern_event.h>
#include <sys/sys_domain.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>

#include <mach/mach.h>
#include <mach/mach_types.h>
#include <mach/i386/thread_status.h>
#include <mach/mach_vm.h>

#include "shared_data.h"

static int g_socket = -1;

int main(int argc, const char * argv[])
{
    struct sockaddr_ctl sc = { 0 };
    struct ctl_info ctl_info = { 0 };
    int ret = 0;
    
    g_socket = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (g_socket < 0)
    {
        perror("creating socket");
        exit(1);
    }
    // the control ID is dynamically generated so we must obtain sc_id using ioctl
    memset(&ctl_info, 0, sizeof(ctl_info));
    strncpy(ctl_info.ctl_name, BUNDLE_ID, MAX_KCTL_NAME);
    ctl_info.ctl_name[MAX_KCTL_NAME-1] = '\0';
	if (ioctl(g_socket, CTLIOCGINFO, &ctl_info) == -1)
    {
		perror("ioctl CTLIOCGINFO");
		exit(1);
	}
    else
		printf("ctl_id: 0x%x for ctl_name: %s\n", ctl_info.ctl_id, ctl_info.ctl_name);

    bzero(&sc, sizeof(struct sockaddr_ctl));
	sc.sc_len = sizeof(struct sockaddr_ctl);
	sc.sc_family = AF_SYSTEM;
	sc.ss_sysaddr = AF_SYS_CONTROL;
	sc.sc_id = ctl_info.ctl_id;
	sc.sc_unit = 0;
    
    ret = connect(g_socket, (struct sockaddr*)&sc, sizeof(sc));
    if (ret)
    {
        perror("connect");
        exit(1);
    }
    
    // add a target to the kernel list
    if (!ret)
    {
        char *target = "Dash";
        ret = setsockopt(g_socket, SYSPROTO_CONTROL, ADD_APP, (void*)target, (socklen_t)strlen(target)+1);
        if (ret)
            printf("socket send failed!\n");
    }
    pid_t pid;
    ssize_t n;
    // loop and get target processes from kernel
    while ((n = recv(g_socket, &pid, sizeof(pid_t), 0)))
    {
        printf("[INFO] Received pid for target process is %d\n", pid);
        mach_port_t task;
        kern_return_t ret = 0;
        ret = task_for_pid(mach_task_self(), pid, &task);
        if (ret)
        {
            printf("task for pid failed!\n");
            continue;
        }
        
        // do whatever processing and patching we need to the target
        uint16_t patch1 = 0x9090;
        mach_msg_type_number_t len = 2;
#define TARGET_ADDRESS 0
        // change memory protection to writable
        mach_vm_protect(task, (mach_vm_address_t)TARGET_ADDRESS, len, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        // patch the process
        ret = mach_vm_write(task, (mach_vm_address_t)TARGET_ADDRESS, (vm_offset_t)&patch1, len);
        if (ret)
        {
            printf("mach vm write failed! %d\n", ret);
        }
        // restore original protection
        mach_vm_protect(task, (mach_vm_address_t)TARGET_ADDRESS, len, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        // not sure why I added this small pause, maybe some test?
        sleep(2);
        // resume process
        kill(pid, SIGCONT);
    }
    printf("[INFO] My work is done, see you later!\n");
    return 0;
}

