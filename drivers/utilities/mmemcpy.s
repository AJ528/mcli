
  @ tells the assembler to use the unified instruction set
  .syntax unified
  @ this directive selects the thumb (16-bit) instruction set
  .thumb
  @ this directive specifies the following symbol is a thumb-encoded function
  .thumb_func
  @ align the next variable or instruction on a 2-byte boundary
  .balign 2
  @ make the symbol visible to the linker
  .global memcpy_
  @ marks the symbol as being a function name
  .type memcpy_, STT_FUNC
memcpy_:
@ r0 = destination addr
@ r1 = source addr
@ r2 = num bytes
@ returns destination addr in r0

  cmp   r2, #0              @ if there are 0 bytes to move
  beq   no_pop_exit                @ exit
  push  {r4, r5}        @ store r4 & r5 values on stack

  movs  r5, #3              @ put a 3 in r5. This is used later to test for 4-byte alignment.
  subs  r4, r0, r1          @ subtract source addr from destination addr, update flags, and store result in r4
  beq   exit                @ if source=destination, nothing to do

  mov   r3, r0              @ copy destination addr into r3
  cmp   r2, #4              @ check if there are 4+ bytes to copy
  blo   copy_single         @ if not, copy one at a time
  cmp   r2, #16             @ check if there are 16+ bytes to copy
  blo   quad_byte_copy      @ if not, copy 4 bytes at a time
  tst   r4, r5              @ check if dest-source is a multiple of 4
  bne   quad_byte_copy      @ if not, copy 4 bytes at a time
  tst   r1, r5              @ check if the source address is 4-byte aligned
  beq   quad_word_copy      @ if so, the data can be copied 4 words at a time

@ if dest-source is a multiple of 4, but source is not 4-byte aligned, we can
@ copy the first few bytes individually until source and destination are aligned.
align_chunk:
  ldrb  r4, [r1]            @ load byte from memory[r1] into r4
  strb  r4, [r3]            @ store r4 byte into memory[r3]
  adds  r1, r1, #1
  adds  r3, r3, #1
  subs  r2, r2, #1          @ decrement remaining bytes by 1
  tst   r1, r5              @ check if the source address is 4-byte aligned
  bne   align_chunk         @ if not, repeat
  cmp   r2, #16             @ check if there are 16+ bytes to copy
  blo   quad_byte_copy      @ if not, copy 4 bytes at a time

@ there are 16 or more bytes to copy and the src and dest are 4-byte aligned, so we can copy word-wise
quad_word_copy:
  push  {r6, r7}            @ store r6 & r7 values on stack
.balign 4                   @ align the loop to an 4 byte boundary
qwc_loop:
  ldm   r1!, {r4-r7}        @ load r4-r7 with words from memory[r1], then increment r1 by 16
  stm   r3!, {r4-r7}        @ store r4-r7 values into memory[r3], then increment r3 by 16
  subs  r2, r2, #16         @ decrement remaining bytes by 16
  cmp   r2, #16
  bhs   qwc_loop    @ if there are 16+ bytes left, quad word copy again
  pop   {r6, r7}          @ if not, restore r6 & r7, we are done using them
  cmp   r2, #4
  bhs   quad_byte_copy         @ if there are 4+ bytes left, quad byte copy
  cmp   r2, #0              @ check if there are 0 bytes left to copy
  bne   copy_single     @ if not, finish with single byte copying
  b     exit                @ otherwise, exit


.balign 8                   @ align the loop to an 8 byte boundary
  nop
  nop
quad_byte_copy:                @ copy 4 bytes at a time
  ldrb  r4, [r1]            @ load byte from memory[r1] into r4
  strb  r4, [r3]            @ store r4 byte into memory[r3]
  ldrb  r4, [r1, #1]
  strb  r4, [r3, #1]
  ldrb  r4, [r1, #2]
  strb  r4, [r3, #2]
  ldrb  r4, [r1, #3]
  strb  r4, [r3, #3]
  adds  r1, r1, #4          @ increment r1 and r3 by 4
  adds  r3, r3, #4
  subs  r2, r2, #4          @ decrement remaining bytes by 4
  cmp   r2, #4              @ check if there are 4 or more bytes to copy
  bhs   quad_byte_copy         @ if so, quad copy again

  cmp   r2, #0              @ check if there are 0 bytes left to copy
  beq   exit                @ if so, exit
@ otherwise, there are <4 bytes left to copy

copy_single:
  ldrb  r4, [r1]            @ load byte from memory[r1] into r4
  strb  r4, [r3]            @ store r4 byte into memory[r3]
  adds  r1, r1, #1
  adds  r3, r3, #1
  subs  r2, r2, #1          @ decrement remaining bytes by 1
  bne   copy_single         @ if bytes are left, repeat

exit:
  pop   {r4, r5}        @ restore previous value of r4 & r5
no_pop_exit:
  bx    lr                  @ exit function

  .size memcpy_, . - memcpy_

