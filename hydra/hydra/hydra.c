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
 * hydra.c
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

#include <mach/mach_types.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <string.h>
#include <sys/systm.h>
#include <stdbool.h>
#include <sys/param.h>
#include <stdint.h>
#include <sys/kern_control.h>

#include "my_data_definitions.h"
#include "kernel_info.h"
#include "cpu_protections.h"
#include "suspend_proc.h"
#include "kernel_control.h"

kern_return_t hydra_start(kmod_info_t * ki, void *d);
kern_return_t hydra_stop(kmod_info_t *ki, void *d);

// global vars to hold original bytes, targets, and info to solve symbols
char g_original_bytes[12];
targets_t g_targets_list = NULL;
struct kernel_info g_kernel_info;
mach_vm_address_t g_hook_symbol;

/*
 * where the fun begins
 */
kern_return_t
hydra_start(kmod_info_t * ki, void *d)
{
    /*
     * first step: retrieve necessary kernel information so we can solve the symbols
     * we need:
     * - find kernel aslr
     * - patch kernel function to suspend processes
     * - solve symbols
     */
    if (init_kernel_info(&g_kernel_info) != KERN_SUCCESS)
    {
        return KERN_FAILURE;
    }
    g_hook_symbol = solve_kernel_symbol(&g_kernel_info, "_proc_resetregister");
    if (g_hook_symbol == 0)
    {
        LOG_MSG("[ERROR] Failure to solve proc_resetregister() symbol...\n");
        return KERN_FAILURE;
    }
    // first we need to store the original bytes
    memcpy(g_original_bytes, (void*)g_hook_symbol, 12);
    // now we can overwrite with the jump to our function
    disable_wp();
    disable_interrupts();
    char trampoline[12] = "\x48\xB8\x00\x00\x00\x00\x00\x00\x00\x00" // mov rax, address
                          "\xFF\xE0";                                // jmp rax
    mach_vm_address_t patch_address = (mach_vm_address_t)myproc_resetregister;
#if DEBUG
    LOG_MSG("[DEBUG] address of my proc: %llx\n", (uint64_t)myproc_resetregister);
#endif
    // add the address of our proc_resetregister function to the trampoline
    memcpy(trampoline+2, &patch_address, 8);
    // and finally patch the kernel code
    memcpy((void*)g_hook_symbol, trampoline, 12);
    enable_wp();
    enable_interrupts();
    // implement the communication channel with userland
    start_kern_control();
    // startup work is done, userland daemon is in charge
    return KERN_SUCCESS;
}

/*
 * no more fun :-(
 */
kern_return_t
hydra_stop(kmod_info_t *ki, void *d)
{
    // restore original bytes of proc_resetregister
    disable_wp();
    disable_interrupts();
    memcpy((void*)g_hook_symbol, g_original_bytes, 12);
    enable_wp();
    enable_interrupts();
    // remove kernel control channel
    stop_kern_control();
    // all done, bye bye to hydra!
    return KERN_SUCCESS;
}
