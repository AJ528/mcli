#ifndef __MCLI_H
#define __MCLI_H

// whenever a text character is received, call `cli_input()` and pass it the character
void cli_input(char c);
// put `cli_process()` somewhere in the superloop so it is called regularly
// this processes incoming/outgoing text
void cli_process(void);

#endif /* __MCLI_H */
