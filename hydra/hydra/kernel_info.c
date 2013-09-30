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
 * kernel_info.c
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

#include "kernel_info.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/vnode.h>
#include <string.h>
#include <sys/attr.h>
#include <mach-o/nlist.h>
#include <mach-o/loader.h>

#include "proc.h"
#include "idt.h"

static int get_kernel_mach_header(void *buffer, vnode_t kernel_vnode);
static int process_mach_header(void *kernel_header, kernel_info_t kernel_info);
static int get_kernel_linkedit(vnode_t kernel_vnode, kernel_info_t kernel_info);
static mach_vm_address_t calculate_int80address(const mach_vm_address_t idt_address);
static mach_vm_address_t get_running_text_address(void);
static mach_vm_address_t find_kernel_base(const mach_vm_address_t int80_address);

#pragma mark Public functions

/*
 * entrypoint function to read necessary information from running kernel and kernel at disk
 * such as kaslr slide, linkedit location
 */
kern_return_t
init_kernel_info(kernel_info_t kernel_info)
{
    kern_return_t error = 0;
    // lookup vnode for /mach_kernel
    vnode_t kernel_vnode = NULLVP;
    
    error = vnode_lookup("/mach_kernel", 0, &kernel_vnode, NULL);
    if (error)
    {
        LOG_MSG("[ERROR] Kernel vnode lookup failed!\n");
        return KERN_FAILURE;
    }

    void *kernel_header = _MALLOC(PAGE_SIZE_64, 1, M_ZERO);
    if (kernel_header == NULL)
    {
        LOG_MSG("[ERROR] Failed to allocate memory for kernel mach-o header!\n");
        goto failure;
    }
    // read and process kernel header from filesystem
    if (get_kernel_mach_header(kernel_header, kernel_vnode) ||
        process_mach_header(kernel_header, kernel_info))
    {
        goto failure;
    }
    // compute kaslr slide
    kernel_info->running_text_addr = get_running_text_address();
    kernel_info->kaslr_slide = kernel_info->running_text_addr - kernel_info->disk_text_addr;
#if DEBUG
    LOG_MSG("[DEBUG] kernel aslr slide is %llx\n", kernel_info->kaslr_slide);
#endif
    // we know the location of linkedit and offsets into symbols and their strings
    // now we need to read linkedit into a buffer so we can process it
    // __LINKEDIT total size is around 1MB
    kernel_info->linkedit_buf = _MALLOC(kernel_info->linkedit_size, 1, M_ZERO);
    if (kernel_info->linkedit_buf == NULL)
    {
        LOG_MSG("[ERROR] Failed to allocate memory for linkedit buffer!\n");
        goto failure;
    }
    // read linkedit from filesystem
    if (get_kernel_linkedit(kernel_vnode, kernel_info))
    {
        goto failure;
    }

success:
    _FREE(kernel_header, M_ZERO);
    vnode_put(kernel_vnode);
    return KERN_SUCCESS;
    
failure:
    if (kernel_header != NULL)
    {
        _FREE(kernel_header, M_ZERO);
    }
    vnode_put(kernel_vnode);
    return KERN_FAILURE;
}

/*
 * function to solve a kernel symbol
 */
mach_vm_address_t
solve_kernel_symbol(kernel_info_t kernel_info, char *symbol_to_solve)
{
    struct nlist_64 *nlist = NULL;
    kernel_info_t ki = kernel_info;
    
    if (ki == NULL || ki->linkedit_buf == NULL)
    {
        LOG_MSG("[ERROR] Kernel info struct is NULL or no linkedit buffer available!\n");
        return 0;
    }

    mach_vm_address_t symbol_offset = ki->symboltable_fileoffset - ki->linkedit_fileoffset;
    mach_vm_address_t string_offset = ki->stringtable_fileoffset - ki->linkedit_fileoffset;

    for (int i = 0; i < ki->symboltable_nr_symbols; i++)
    {
        nlist = (struct nlist_64*)((char*)ki->linkedit_buf + symbol_offset + i * sizeof(struct nlist_64));
        char *symbol_string = ((char*)ki->linkedit_buf + string_offset + nlist->n_un.n_strx);
        // find if symbol matches
        if (strncasecmp(symbol_to_solve, symbol_string, strlen(symbol_to_solve)) == 0)
        {
#if DEBUG
            LOG_MSG("[DEBUG] found kernel symbol %s at %p\n", symbol_to_solve, (void*)nlist->n_value);
#endif
            return (nlist->n_value + ki->kaslr_slide);
        }
    }
    return 0;
}

#pragma Local functions to get data from filesystem /mach_kernel

/*
 * retrieve the first page of kernel binary at disk into input buffer
 * XXX: only ready for Mountain Lion since it assumes kernel image is non-fat
 *      not hard to make it compatible with fat images
 */
static int
get_kernel_mach_header(void *buffer, vnode_t kernel_vnode)
{
    int error = KERN_SUCCESS;
    uio_t uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
    if (uio == NULL)
    {
        LOG_MSG("[ERROR] uio_create returned null!\n");
        return KERN_FAILURE;
    }
    error = uio_addiov(uio, CAST_USER_ADDR_T(buffer), PAGE_SIZE_64);
    if (error)
    {
        LOG_MSG("[ERROR] uio_addiov returned error!\n");
        return KERN_FAILURE;
    }
    // read kernel vnode into the buffer
    error = VNOP_READ(kernel_vnode, uio, 0, NULL);
    if (error)
    {
        LOG_MSG("[ERROR] VNOP_READ failed!\n");
        return KERN_FAILURE;
    }
    else if (uio_resid(uio))
    {
        return EINVAL;
    }
    
    return KERN_SUCCESS;
}

/*
 * retrieve the whole linkedit segment into target buffer from kernel binary at disk
 */
static int
get_kernel_linkedit(vnode_t kernel_vnode, kernel_info_t kernel_info)
{
    int error = 0;
    uio_t uio = uio_create(1, kernel_info->linkedit_fileoffset, UIO_SYSSPACE, UIO_READ);
    if (uio == NULL)
    {
        LOG_MSG("[ERROR] uio_create returned null!\n");
        return KERN_FAILURE;
    }
    
    error = uio_addiov(uio, CAST_USER_ADDR_T(kernel_info->linkedit_buf), kernel_info->linkedit_size);
    if (error)
    {
        LOG_MSG("[ERROR] uio_addiov returned error!\n");
        return KERN_FAILURE;
    }

    error = VNOP_READ(kernel_vnode, uio, 0, NULL);
    if (error)
    {
        LOG_MSG("[ERROR] VNOP_READ failed!\n");
        return KERN_FAILURE;
    }
    else if (uio_resid(uio))
    {
        return EINVAL;
    }
    
    return KERN_SUCCESS;
}

#pragma Local functions to read kernel Mach-O header

/*
 * retrieve necessary information from the kernel at disk
 */
static int
process_mach_header(void *kernel_header, kernel_info_t kernel_info)
{
    // now we can iterate over the kernel headers
    struct mach_header_64 *mh = (struct mach_header_64*)kernel_header;
    struct load_command *load_cmd = NULL;
    // point to the first load command
    char *load_cmd_addr = (char*)kernel_header + sizeof(struct mach_header_64);
    // iterate over all load cmds and retrieve required info to solve symbols
    // __LINKEDIT location and symbol table location
    for (int i = 0; i < mh->ncmds; i++)
    {
        load_cmd = (struct load_command*)load_cmd_addr;
        if (load_cmd->cmd == LC_SEGMENT_64)
        {
            struct segment_command_64 *seg_cmd = (struct segment_command_64*)load_cmd;
            // use this one to retrieve the original vm address of __TEXT so we can compute aslr slide
            if (strncmp(seg_cmd->segname, "__TEXT", 16) == 0)
            {
                kernel_info->disk_text_addr = seg_cmd->vmaddr;
            }
            else if (strncmp(seg_cmd->segname, "__LINKEDIT", 16) == 0)
            {
                kernel_info->linkedit_fileoffset = seg_cmd->fileoff;
                kernel_info->linkedit_size       = seg_cmd->filesize;
            }
        }
        else if (load_cmd->cmd == LC_SYMTAB)
        {
            struct symtab_command *symtab_cmd = (struct symtab_command*)load_cmd;
            kernel_info->symboltable_fileoffset = symtab_cmd->symoff;
            kernel_info->symboltable_nr_symbols = symtab_cmd->nsyms;
            kernel_info->stringtable_fileoffset = symtab_cmd->stroff;
            kernel_info->stringtable_size       = symtab_cmd->strsize;
        }
        load_cmd_addr += load_cmd->cmdsize;
    }
    return KERN_SUCCESS;
}

#pragma Local functions to find address of running kernel and find kernel ASLR slide

/*
 * retrieve the __TEXT address of current loaded kernel so we can compute the KASLR slide
 */
static mach_vm_address_t
get_running_text_address(void)
{
    // retrieves the address of the IDT
    mach_vm_address_t idt_address = 0;
    get_addr_idt(&idt_address);
    // calculate the address of the int80 handler
    mach_vm_address_t int80_address = calculate_int80address(idt_address);
    // search backwards for the kernel base address (mach-o header)
    mach_vm_address_t kernel_base = find_kernel_base(int80_address);
    // get the vm address of __TEXT segment
    if (kernel_base != 0)
    {
        // get the vm address of __TEXT segment
        struct mach_header_64 *mh = (struct mach_header_64*)kernel_base;
        struct load_command *load_cmd = NULL;
        char *load_cmd_addr = (char*)kernel_base + sizeof(struct mach_header_64);
        for (uint32_t i = 0; i < mh->ncmds; i++)
        {
            load_cmd = (struct load_command*)load_cmd_addr;
            if (load_cmd->cmd == LC_SEGMENT_64)
            {
                struct segment_command_64 *seg_cmd = (struct segment_command_64*)load_cmd;
                if (strncmp(seg_cmd->segname, "__TEXT", 16) == 0)
                {
                    return seg_cmd->vmaddr;
                }
            }
            load_cmd_addr += load_cmd->cmdsize;
        }
    }
    // return 0 in case of failure
    return 0;
}

/*
 * calculate the address of the kernel int80 handler
 * using the IDT array
 */
static mach_vm_address_t
calculate_int80address(const mach_vm_address_t idt_address)
{
  	// find the address of interrupt 0x80 - EXCEP64_SPC_USR(0x80,hi64_unix_scall) @ osfmk/i386/idt64.s
	struct descriptor_idt *int80_descriptor = NULL;
	mach_vm_address_t int80_address = 0;
    // we need to compute the address, it's not direct
    // extract the stub address
    // retrieve the descriptor for interrupt 0x80
    // the IDT is an array of descriptors
    int80_descriptor = (struct descriptor_idt*)(idt_address+sizeof(struct descriptor_idt)*0x80);
    uint64_t high = (unsigned long)int80_descriptor->offset_high << 32;
    uint32_t middle = (unsigned int)int80_descriptor->offset_middle << 16;
    int80_address = (mach_vm_address_t)(high + middle + int80_descriptor->offset_low);
#if DEBUG
	LOG_MSG("[DEBUG] Address of interrupt 80 stub is %llx\n", int80_address);
#endif
    return int80_address;
}

/*
 * find the kernel base address (mach-o header)
 * by searching backwards using the int80 handler as starting point
 */
static mach_vm_address_t
find_kernel_base(const mach_vm_address_t int80_address)
{
    mach_vm_address_t temp_address = int80_address;
    struct segment_command_64 *segment_command = NULL;
    
    while (temp_address > 0)
    {
        if (*(uint32_t*)(temp_address) == MH_MAGIC_64)
        {
            // make sure it's the header and not some reference to the MAGIC number
            segment_command = (struct segment_command_64*)(temp_address+sizeof(struct mach_header_64));
            if (strncmp(segment_command->segname, "__TEXT", 16) == 0)
            {
#if DEBUG
                LOG_MSG("[DEBUG] Found kernel mach-o header address at %p\n", (void*)(temp_address));
#endif
                return (mach_vm_address_t)temp_address;
            }
        }
        temp_address--;
    }
    return 0;
}
