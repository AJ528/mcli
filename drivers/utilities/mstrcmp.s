
  @ tells the assembler to use the unified instruction set
  .syntax unified
  @ this directive selects the thumb (16-bit) instruction set
  .thumb
  @ this directive specifies the following symbol is a thumb-encoded function
  .thumb_func
  @ align the next variable or instruction on a 2-byte boundary
  .balign 2
  @ make the symbol visible to the linker
  .global strcmp_
  @ marks the symbol as being a function name
  .type strcmp_, STT_FUNC
strcmp_:
@ r0 = pointer to the beginning of str1
@ r1 = pointer to the beginning of str2
@ returns str1[n]-str2[n], where n is the point where the strings differ

  subs  r2, r0, r1          @ subtract str2_addr from str1_addr and store in r2
  beq   same_str_exit       @ if pointers are the same, exit
  push  {r4, r5, r6}

  lsls  r2, r2, #30         @ look at the 2 least-significant bits
  bne   byte_compare        @ if str1_addr - str2_addr is not a multiple of 4, must compare bytewise
  lsls  r2, r0, #30         @ look at the 2 least-significant bits of str1_addr
  beq   word_compare        @ if aligned to 4 bytes, we can compare wordwise

@ if str1_addr - str2_addr is a multiple of 4, but str1_addr is not 4-byte aligned, 
@ it is possible to compare wordwise once we achieve 4-byte alignment
@ compare bytewise until aligned
align_to_word:
  ldrb  r2, [r0]            @ load a byte from memory[r0] into r2
  ldrb  r3, [r1]            @ load a byte from memory[r1] into r3
  adds  r0, r0, #1          @ increment r0 and r1 by 1
  adds  r1, r1, #1
  cmp   r2, #0              @ check if r2 is a null (0) byte. This indicates end of string
  beq   sub_and_exit        @ if so, exit
  cmp   r2, r3              @ if r2 is not 0, check if r2 and r3 match
  bne   sub_and_exit        @ if they do not match, exit
  lsls  r4, r0, #30         @ r2 and r3 matched, now check if str1_addr is 4-byte aligned
  bne   align_to_word       @ if not aligned, repeat the loop

word_compare:               @ compare the strings 4 bytes at a time
  ldr   r5, =#0x01010101    @ put 0x01010101 into r5 to later check for nulls
  lsls  r6, r5, #7          @ put 0x80808080 into r6 to later check for nulls
wc_loop:
  ldm   r0!, {r2}           @ load the word r0 points to into r2, and increment r0 by 4 after
  ldm   r1!, {r3}           @ load the word r1 points to into r3, and increment r1 by 4 after
  
  @ test if r2 contains any null bytes (0x00) using the algorithm found here: https://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
  subs  r4, r2, r5          @ do r2 - 0x01010101 and store it in r4
  bics  r4, r4, r2          @ do r4 & ~(r2) and store it in r4
  ands  r4, r4, r6          @ do r4 & 0x80808080 and store it in r4
                            @ if there were any null bytes in r2, r4 will be nonzero in the null-byte byte
  bne   find_different_byte @ find which byte is null
  cmp   r2, r3              @ there were no null bytes, so now see if r2 and r3 match
  beq   wc_loop             @ the two words matched, now loop and do it again   

@ the find_different_byte algorithm assumes little-endian memory
find_different_byte:        @ a null byte was found or r2 and r3 did not match, find which of the 4 bytes is different
                            @ "target byte" = least-significant byte (located at the lowest memory address) of r2 and r3
  uxtb  r0, r2              @ put the target byte into r0 and r1
  uxtb  r1, r3
  subs  r0, r0, r1          @ store the difference between r0 and r1 in r0. If they match, this value is 0
  uxtb  r1, r4              @ put the least-significant byte of r4 into r1. The value will be nonzero if the target byte of r2 is null
  orrs  r1, r0, r1          @ OR r0 and r1 together. If the target bytes were different, r0 is nonzero
                            @ if the target byte in r2 was null, then r1 is nonzero
  bne   exit                @ if either is true, exit

                            @ at this point we know the target bytes matched and were not null. Now look at [15:8] of r2 and r3.
  uxth  r0, r2              @ put the new target byte into [15:8] of r0 and r1
  uxth  r1, r3              @ the old target bytes are in [7:0], but they match so it's okay
  subs  r0, r0, r1          @ store the difference between r0 and r1 in r0. If they match, this value is 0
  asrs  r0, r0, #8          @ shift the result 8 bits to the right since we were looking at [15:8]
  uxth  r1, r4              @ put the least-significant half word of r4 into r1. The value will be nonzero if the target byte of r2 is null
  orrs  r1, r0, r1          @ OR r0 and r1 together. If the target bytes were different, r0 is nonzero
                            @ if the target byte in r2 was null, then r1 is nonzero
  bne   exit                @ if either is true, exit

                            @ at this point we know the target bytes matched and were not null. Now look at [23:16] of r2 and r3.
  lsls  r0, r2, #8          @ put the lowest 24 bits of r2 into the upper 24 bits of r0
  lsrs  r0, r0, #24         @ shift r0 24 bits to the right. Now the target byte is sitting in the lower 8 bits of r0
  lsls  r1, r3, #8          @ do the same thing with r1 and r3
  lsrs  r1, r1, #24
  subs  r0, r0, r1          @ store the difference between r0 and r1 in r0. If they match, this value is 0
  lsls  r1, r4, #8          @ put the lowest 24 bits of r4 into the upper 24 bits of r1. This will be nonzero if the target byte of r2 is null
  orrs  r1, r0, r1          @ OR r0 and r1 together. If the target bytes were different, r0 is nonzero
                            @ if the target byte in r2 was null, then r1 is nonzero
  bne   exit                @ if either is true, exit

                            @ at this point we know the target bytes matched and were not null. The difference must be in the most-significant byte
  lsrs  r0, r2, #24         @ put the most-significant byte of r2 and r3 into r0 and r1, respectively
  lsrs  r1, r3, #24
  subs  r0, r0, r1          @ store the difference between r0 and r1 in r0
  b     exit                @ always exit

same_str_exit:              @ r0 and r1 point to the same address, so they must match
  movs  r0, #0              @ put a 0 in r0 and exit
  b     no_pop_exit

byte_compare:               @ str1_addr-str2_addr is not a multiple of 4, must compare bytewise
  ldrb  r2, [r0]            @ load byte at memory[r0] into r2
  ldrb  r3, [r1]            @ load byte at memory[r1] into r3
  adds  r0, r0, #1          @ increment r0 and r1 by 1
  adds  r1, r1, #1
  cmp   r2, #0              @ check if r2 is a null byte
  beq   sub_and_exit        @ if so, create return value and exit
  cmp   r2, r3              @ if not, check if r2 matches r3
  beq   byte_compare        @ if so, repeat and look at the next pair of bytes
                            @ if not, create return value and exit
sub_and_exit:
  subs  r0, r2, r3          @ create the return value by subtracting r3 from r2 and storing in r0
exit:
  pop   {r4, r5, r6}        @ restore previous value of r4, r5, & r6
no_pop_exit:
  bx    lr                  @ exit function

  .size strcmp_, . - strcmp_

