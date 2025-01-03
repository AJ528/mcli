#!/bin/bash

# this bash script is called by "make debug"

echo "START debug.sh"

# start the gdb client, connect to the remote server, load the program onto
# the target, set a breakpoint at main, and enable the text user interface
# also disable confirmations so you can quit by just using "q"
gdb-multiarch -ex "set confirm off" \
    -ex "target extended-remote /dev/ttyACM0" -ex "monitor swd_scan" \
    -ex "attach 1" -ex "load" -ex "break main" \
    bin/mcli.elf

echo "DONE"
