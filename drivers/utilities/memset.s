/*
 * memset - fill memory with a constant
 *
 * Copyright (c) 2010-2021, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

	.syntax unified
	.thumb

@ ---------------------------------------------------------------------------
	.thumb_func
	.align 2
	.global memset
	.type memset, STT_FUNC
memset:
	@ r0 = address
	@ r1 = character
	@ r2 = count
	@ returns original address in r0

	mov	r3, r0		@ copy address into r3
	cbz	r2, 10f		@ Exit if 0 length

	tst	r0, #3	@ test if aligned to 4-bytes
	beq	2f		@ jump if true

	@ Ok, so we're misaligned here
1:
	@ copy the character (r1) into the copy address, then inc copy address
	strb	r1, [r3], #1	
	subs	r2,r2,#1		@ decrement length by 1
	tst	r3, #3		@ test for 4-byte alignment
	cbz	r2, 10f		@ Exit if we hit the end
	bne	1b		@ go round again if still misaligned

2:
	@ OK, so we're aligned
	@ store the existing values on the stack so we can use r4-r7
	push	{r4,r5,r6,r7}	
	bics	r4, r2, #15	@ if less than 16 bytes then need to finish it off
	beq	5f

3:
	@ POSIX says that the char is cast to an unsigned char.  A uxtb is one
	@ byte and takes two cycles, where an AND is four bytes but one cycle
	and	r1, #0xFF
	@ copy the lowest byte of r1 into all 4 bytes of r1
	@ bitwise OR (r1) and (r1 << 8) and store in r1
	orr	r1, r1, r1, lsl#8
	@ bitwise OR (r1) and (r1 << 16) and store in r1
	orr	r1, r1, r1, lsl#16
	@ copy r1 into r5-r7
	mov	r5,r1
	mov	r6,r1
	mov	r7,r1

4:
	@ subtract 16 from r4 and store in r4
	subs	r4,r4,#16
	@ stmia stores multiple registers starting at the address in r3. r3 is 
	@ incremented after each register is stored, and r3 is updated to hold
	@ this new incremented value. r1, r5, r6, and r7 are stored.
	stmia	r3!,{r1,r5,r6,r7}
	@ repeat if there are more than 16 bytes remaining
	bne	4b
	@ at this point there is less than 16 bytes remaining
	@ update r2 to show how many bytes are left
	and	r2,r2,#15

	@ At this point we're still aligned and we have upto align-1 bytes left to right
	@ we can avoid some of the byte-at-a time now by testing for some big chunks
	tst	r2,#8
	@ if-then block, which is executed if r2 >= 8
	@ (instructions in if-then blocks must end matching the condition (ne))
	itt	ne
	@ subtracts 8 from r2 and stores in r2
	subne	r2,r2,#8
	@ store multiple registers and increment after each one
	stmiane	r3!,{r1,r5}

5:
	@ restore the previous values of r4-r7
	pop	{r4,r5,r6,r7}
	@ if there are no more bytes left, jump to the end
	cbz	r2, 10f

	@ Got to do any last < alignment bytes
6:
	subs	r2,r2,#1		@ decrement r2
	strb	r1,[r3],#1		@ store the byte of r1 at r3, increment r3
	bne	6b					@ repeat if r2 is not 0

10:
	bx	lr		@ goodbye
	.size	memset, . - memset
