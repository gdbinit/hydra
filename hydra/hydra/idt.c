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
 * idt.c
 *
 * Functions to deal with IDT table
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

#include "idt.h"

// retrieve the address of the IDT
void
get_addr_idt(mach_vm_address_t *idt)
{
	uint8_t idtr[10];
	__asm__ volatile ("sidt %0": "=m" (idtr));
	*idt = *(mach_vm_address_t *)(idtr+2);
}

// retrieve the size of the IDT
uint16_t 
get_size_idt(void)
{
	uint8_t idtr[10];
    uint16_t size = 0;
	__asm__ volatile ("sidt %0": "=m" (idtr));
	size = *((uint16_t *) &idtr[0]);
	return(size);
}
