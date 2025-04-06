# mcli
Simple command line interface for embedded systems

### Status 
Does everything I need it to. Will fix bugs as I come across them.

### How to use this in an embedded project
1. Copy `mcli.c`, `mcli.h`, `mprint.c` and `mprint.h` into your project.
2. Copy `mmemmcpy.s`, `mmemmove.s`, and `mstrcmp.s` into your project (or use the standard libc version if you're not using a Cortex-M microcontroller).
3. In your program: 
  - Call `cli_input()` whenever your text interface receives a character. Pass that character to `cli_input()`.
  - Create a function `int32_t putchar_(char c)` that outputs character `c` to the text interface every time it is called.
  - Put `cli_process()` somewhere in your superloop so it is called regularly. This function processes incoming/outgoing text.
4. Done! 

### How to measure the size of this project
1. copy these files into an embedded project.
2. compile the project.
3. measure the size of the relevant object files using `size -A`.
4. the total size is found from summing all relevant files together.


### Details

`mcli` supports backspace, cursor movement with the left/right arrow keys, and history navigation with the up/down arrow keys. Auto-completion is not supported.

`mcli.c` is about 390 lines of code (per David A. Wheeler's `SLOCCount`). The code was written with Cortex-M microcontrollers in mind. `mmemcpy.s`, `mmemmove.s`, and `mstrcmp.s` are duplications of their libc counterparts, written in Arm assembly for Cortex-M microcontrollers. If you want to use this code on other platforms, simply replace those function calls with the standard library version.

The table below shows the compiled size of the necessary modules using `arm-none-eabi-gcc` with `-Os` optimizations. The main `mcli` module includes a 1024-byte history buffer by default, but that can be shrunk (or removed) for RAM-constrained applications. `mprintf` includes a 512-byte output buffer than can also be shrunk (at the cost of printing shorter lines).

| Module     | Flash Usage (bytes)   | RAM Usage (bytes)  |
| ---------- | --------------------- | ------------------ |
| `mcli`     | 1345                  | 1334               |
| `mprintf`  | 1578                  | 512                |
| `mmemcpy`  | 132                   | 0                  |
| `mmemmove` | 228                   | 0                  |
| `mstrcmp`  | 132                   | 0                  |
| **Total**  | **4713**              | **1846**           |

