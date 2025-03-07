
#include "mcli.h"
#include "mprintf.h"
#include "utils.h"

#include <stdint.h>
#include <stdbool.h>

// RX_BUFFER_SIZE must be a power of 2
// this lets the read/write indices wrap much faster without using modulo
#define RX_BUFFER_SIZE    128
// CMD_BUFFER_SIZE doesn't need to be a power of 2
#define CMD_BUFFER_SIZE   128
// MAX_NUM_ARGS is the maximum number of arguments allowed to be passed to a command
#define MAX_NUM_ARGS      7
// HISTORY_SIZE must be a multiple of 4
// HISTORY_SIZE determines how many commands can be held in history before the oldest is freed
#define HISTORY_SIZE      1024

/*** Internal Structures ***/
typedef struct {
  // data points to the block of memory where data is stored
  uint8_t *data;
  // size is the maximum size the buffer can hold
  uint32_t size;
  // writeIndex is the location where the next data is written
  uint32_t writeIndex;
  // readIndex is the location where data should be read from
  uint32_t readIndex;
  // overflow is true when you try to push too much data into the buffer
  bool overflow;
} ringBuf;

typedef struct {
  // data points to the block of memory where data is stored
  char *data;
  // size is the maximum size the buffer can hold
  uint32_t size;
  // len is the current length of the text in the buffer
  uint32_t len;
  // cursorOffset is how far the cursor is from the end of the entered text.
  // 0 means the cursor is all the way to the right
  uint32_t cursorOffset;
} txtBuf;

typedef struct cmd_hist {
  // cmd_len is the length of string cmd
  uint32_t cmd_len;
  // prev points to the previous (newer) command in the linked list
  struct cmd_hist *prev;
  // next points to the next (older) command in the linked list
  struct cmd_hist *next;
  // cmd is variable length string. This is the first character.
  char cmd;
} cmdHistory;

typedef struct {
  const char *const cmd_name;
  int32_t (*func_pointer)(uint32_t argc, char* argv[]);
  const char *const help_text;
} cmdEntry;

/*** Internal Function Definitions ***/
static int32_t parse_command(void);
static int32_t tokenize_command(char* cmd_buffer, uint32_t* argc, char* argv[]);

static void handle_escape_char(char c);
static void handle_printable_char(char c);
static void handle_control_char(char c);

static inline bool isPrintableChar(char c);
static inline bool isBlank(txtBuf *buf);
static inline void reset_cmdBuffer(void);
static inline void print_prompt(void);
static void clear_cmd_line(bool show_prompt);

static void history_display(cmdHistory *hist_cmd);
static void history_input(txtBuf *cmd);
static void* history_malloc(uint32_t byte_request);
static void free_oldest_cmd(void);

static uint8_t bufPop(ringBuf *buf);
static int32_t bufPush(ringBuf *buf, uint8_t value);
static bool bufIsEmpty(ringBuf *buf);

/*** External Functions ***/
// this function is called to output a single character over UART
extern int32_t putchar_(char c);

/*** Command Table Functions ***/
extern int32_t help_cmd(uint32_t argc, char* argv[]);

/*** Internal Variables and Structures ***/
// cmd_table is where command names (what is typed) get mapped
// to function pointers (which function gets called)
static const cmdEntry cmd_table[] =
{
  {
    .cmd_name = "help",
    .func_pointer = help_cmd,
    .help_text = "displays list of builtin commands"
  }
};

// ring buffer to hold received characters until they are processed
static ringBuf rxBuffer = {
  .data = (uint8_t[RX_BUFFER_SIZE]){},
  .size = RX_BUFFER_SIZE,
  .writeIndex = 0,
  .readIndex = 0,
  .overflow = false
};

// cmdBuffer is where the current value of the command line is stored.
// as characters are typed or deleted, this buffer gets modified to match
// the displayed characters
static txtBuf cmdBuffer = {
  .size = CMD_BUFFER_SIZE,
  .len = 0,
  .cursorOffset = 0,
  .data = (char[CMD_BUFFER_SIZE]){0},
};

// the 2 previously entered characters
static char previous_char[2] = {0};

// newest command stored in history
static cmdHistory *newest_cmd = NULL;
// oldest command stored in history
static cmdHistory *oldest_cmd = NULL;
// history command currently being shown
static cmdHistory *current_history_cmd = NULL;


/*** Function Definitions ***/
// this function must be called every time a charcter is received
// it pushes the character into a ring buffer for later processing
void cli_input(char c)
{
  int32_t result = bufPush(&rxBuffer, (uint8_t)c);
  if(result < 0){
    rxBuffer.overflow = true;
  }
}

// this function is called in the superloop. It checks for characters in the 
// ring buffer and processes them accordingly
void cli_process(void)
{
  while(!bufIsEmpty(&rxBuffer)){
    char c = (char)bufPop(&rxBuffer);

    // check for escape sequence
    if((previous_char[0] == 0x1B) && (c == '[')){
      // avoid printing '['. This is technically a printable character, 
      // but in this context it's an escape code
    }else if((previous_char[0] == '[') && (previous_char[1] == 0x1B)){
      handle_escape_char(c);
    }else if(isPrintableChar(c)){
      handle_printable_char(c);
    }else{
      handle_control_char(c);
    }

    // record previous 2 characters
    previous_char[1] = previous_char[0];
    previous_char[0] = c;
  }

  if(rxBuffer.overflow){
    // handle overflow by erasing the current command
    print_newline();
    println_("ERROR: ring buffer overflowed");
    reset_cmdBuffer();
    rxBuffer.overflow = false;
  }
}



static void handle_printable_char(char c)
{
  // cmdBufRWSize is the largest size usable to hold printable characters
  // 1 byte at the end is reserved for '\0'
  static const uint32_t cmdBufRWSize = CMD_BUFFER_SIZE - 1;
  // this escape sequence will shift all chars past the cursor 1 space to the right
  static const char esc_seq_insert_char[] = "\x1B[@";

  // if cmdBuffer is already holding the limit of printable characters
  if(cmdBuffer.len >= cmdBufRWSize){
    return;
  }
  // determine where the new character should be inserted
  uint32_t insert_pos = cmdBuffer.len - cmdBuffer.cursorOffset;
  // move all the characters past the insertion point 1 space to the right
  memmove_(&(cmdBuffer.data[insert_pos+1]), &(cmdBuffer.data[insert_pos]), cmdBuffer.cursorOffset + 1);
  // insert the new character into the command buffer and increment length of array
  cmdBuffer.data[insert_pos] = c;
  cmdBuffer.len++;

  // now handle printing the character to the terminal
  // if this new character is not being appended to the end of the string
  if(cmdBuffer.cursorOffset > 0){
    // send an escape sequence to do the same thing as memmove above: shift all characters right of cursor 1 to the right
    puts_(esc_seq_insert_char);
  } 
  // print the character to the screen
  putchar_(c);
}

static void handle_escape_char(char c)
{
  // this escape sequence moves the cursor 1 space to the right
  static const char esc_seq_cursor_right[] = "\x1B[C";
  // this escape sequence moves the cursor 1 space to the left
  static const char esc_seq_cursor_left[] = "\x1B[D";

  switch(c){
  // cursor up (go back in history)
  case 'A':
    // if there is no history command currently being displayed
    if(current_history_cmd == NULL){
      current_history_cmd = newest_cmd;   // start at the newest command
    }else if(current_history_cmd->next != NULL){    // otherwise, if we can go to the next oldest command
      current_history_cmd = current_history_cmd->next;  // do so
    }
    history_display(current_history_cmd);   // display the current command (even if it's NULL)
    break;
  // cursor down (go forward in history)
  case 'B':
    // cursor down only does something if we're already navigating history
    if(current_history_cmd != NULL){
      current_history_cmd = current_history_cmd->prev;
    }
    history_display(current_history_cmd);
    break;
  // cursor right
  case 'C':
    // if not at the end of the line, move cursor right
    if(cmdBuffer.cursorOffset > 0){
      cmdBuffer.cursorOffset--;
      puts_(esc_seq_cursor_right);
    }
    break;
  // cursor left
  case 'D':
    // if not at the beginning of the line, move cursor left
    if(cmdBuffer.cursorOffset < cmdBuffer.len){
      cmdBuffer.cursorOffset++;
      puts_(esc_seq_cursor_left);
    }
    break;
  default:
    // encountered unsupported character, do nothing
    break;
  }
}

static void handle_control_char(char c)
{
  // this escape sequence moves the cursor 1 space to the left, then deletes that character
  static const char esc_seq_backspace[] = "\x1B[D\x1B[P";
  switch(c){
  case '\n':
    if(previous_char[0] == '\r'){
      // newline was already handled from '\r'
      break;
    }// else fall through
  case '\r':
    // enter was pressed, handle it here!
    print_newline();
    // if the line wasn't blank, store it and process it
    if(!isBlank(&cmdBuffer)){
      history_input(&cmdBuffer);
      current_history_cmd = NULL;   // reset the history command
      parse_command();
    }
    // reset the command buffer
    reset_cmdBuffer();
    print_prompt();
    break;
  case 0x7F:
    // DEL case falls through and is treated the same as the BS case
  case '\b':
    // backspace was pressed, handle it here
    // delete the character from the screen
    puts_(esc_seq_backspace);
    // remove the character from the command buffer
    uint32_t insert_pos = cmdBuffer.len - cmdBuffer.cursorOffset;
    memmove_(&(cmdBuffer.data[insert_pos-1]), &(cmdBuffer.data[insert_pos]), cmdBuffer.cursorOffset + 1);
    cmdBuffer.len--;
    break;
  default:
    // encountered unsupported character, do nothing
    break;
  }
}

// this function returns true if the character is printable, otherwise false
static inline bool isPrintableChar(char c)
{
  // remove most-significant bit
  c = c & 0x7F;
  // if c >= 0x20 and != 0x7F, it is a printable character
  if((c & 0x60) && (c != 0x7F)){
    return true;
  }else{
    return false;
  }
}

// this function returns true if the buffer is blank, otherwise false
static inline bool isBlank(txtBuf *buf)
{
  // look at all characters in the buffer
  for(uint32_t i = 0; i < buf->len; i++){
    // as soon as we encounter one that is printable,
    // we know the buffer isn't blank
    if(isPrintableChar(buf->data[i])){
      return false;
    }
  }
  return true;
}

// reset the state of the command buffer
static inline void reset_cmdBuffer(void)
{
    cmdBuffer.data[0] = '\0';
    cmdBuffer.len = 0;
    cmdBuffer.cursorOffset = 0;
}

// clear the text being show on the command line
static void clear_cmd_line(bool show_prompt)
{
  // clears the entire line, then resets the cursor to the beginning of the line
  static const char esc_seq_clear_line[] = "\x1B[2K\r";
  puts_(esc_seq_clear_line);
  if(show_prompt){
    print_prompt();
  }
}

// print a prompt to the command line. This tells the user they can input text
static inline void print_prompt(void)
{
  puts_("# ");
}

// parse the command buffer by tokenizing the input string, looking to see
// if the first word entered matches any known commands, and if so, calling them
static int32_t parse_command(void)
{
  uint32_t argc;                  // count how many arguments there are
  char* argv[MAX_NUM_ARGS + 1];   // array size is name of command + max number of arguments

  int32_t retval = tokenize_command(cmdBuffer.data, &argc, argv);
  CHECK(retval);

  // look for a matching command name
  // and call it
  for(uint32_t i = 0; i < COUNT_OF(cmd_table); i++){
    if(strcmp_(argv[0], cmd_table[i].cmd_name) == 0){
      retval = (cmd_table[i].func_pointer)(argc, argv);
      CHECK(retval);
      return(0);
    }
  }
  // if we reach this point it means we searched the whole command table and didn't find a match
  println_("ERROR: command not found!");
  return (-1);
}

// tokenize the input string. This is accomplished by going through the string
// and replacing  all spaces (' ') with NULL bytes ('\0')
static int32_t tokenize_command(char* cmd_buffer, uint32_t* argc, char* argv[])
{
  uint32_t i = 0;
  // track the previous character we just looked at
  // initialized to ' ' so we know the first character
  // found is the start of a token
  char previous_c = ' ';
  // initialize c to the beginning of the string
  char c = cmd_buffer[i];
  // initialize the number of arguments to 0
  *argc = 0;

  // until we reach the end of the string
  while(c != '\0'){
    // if we find the beginning of a new word
    if((c != ' ') && (previous_c == ' ')){
      // and we haven't found too many words
      if((*argc) > MAX_NUM_ARGS){
        println_("ERROR: too many arguments passed");
        return (-1);
      }else{
        // store the location of this word
        argv[(*argc)] = &(cmd_buffer[i]);
        // and increment how many words we've found
        (*argc)++;
      }
    // spaces are turned to NULL ('\0') so when the function reads the
    // word starting at argv[n], it knows when to stop
    }else if(c == ' '){
      cmd_buffer[i] = '\0';
    }
    // remember the previous character, increment the count, and do it again
    previous_c = c;
    i++;
    c = cmd_buffer[i];
  }
  // everything was fine so return 0
  return 0;
}

/***** History Functions *****/
// this function clears the current command line and displays a previously-entered command
static void history_display(cmdHistory *hist_cmd)
{
  // reset the command buffer and clear the displayed text
  reset_cmdBuffer();
  clear_cmd_line(true);
  // as long as we're being asked to actually display something, print that to the screen
  if(hist_cmd != NULL){
    memcpy_(cmdBuffer.data, &hist_cmd->cmd, (hist_cmd->cmd_len+1));
    cmdBuffer.len = hist_cmd->cmd_len;
    puts_(cmdBuffer.data);
  }
}

// this function takes the entered command and stores it in the history (as long as it isn't an immediate repeat) 
static void history_input(txtBuf *cmd)
{
  // if the history isn't empty, check for repeats
  if(newest_cmd != NULL){
    if(strcmp_(&newest_cmd->cmd, cmd->data) == 0){  // if the command we want to put into history is already newest_cmd, skip
      return;
    }
  }
  // calculate how many bytes we need to allocate for this new command
  uint32_t size_req = (sizeof(cmdHistory)-3) + (cmd->len);

  // make sure we request bytes in word increments (multiples of 4)
  // I don't think this is technically necessary, but maintaining word-alignment
  // means the memory accesses are a lot cleaner
  while(size_req & 0x03){
    size_req++;
  }
  // allocate memory (if possible) to store the new history entry
  cmdHistory *new_node = (cmdHistory *)history_malloc(size_req);

  // if unable to allocate enough memory, new_node will be NULL
  while(new_node == NULL){
    // if the oldest command is NULL, that means the memory is totally empty
    if(oldest_cmd == NULL){
      return;   // no room to store the command
    }else{
      // free the oldest entry and retry
      free_oldest_cmd();
      new_node = (cmdHistory *)history_malloc(size_req);
    }
  }
  // if you reach this point you were able to allocate enough memory to store the command
  // copy the data from cmdBuffer to the new_node entry
  new_node->cmd_len = cmd->len;
  memcpy_(&new_node->cmd, cmd->data, cmd->len);
  *((char*)&new_node->cmd + cmd->len) = '\0';   // put a null byte on the end of the string
  // new_node doesn't have any entries newer than it, so prev is NULL
  new_node->prev = NULL;
  // new_node is inserted before the newest_cmd entry (which is now technically second-newest)
  new_node->next = newest_cmd;

  if(newest_cmd == NULL){   // if there aren't any commands in the history yet
    oldest_cmd = new_node;  // this entry automatically becomes the oldest
  }else{
    newest_cmd->prev = new_node;  // otherwise, newest_cmd now has a younger brother
  }

  newest_cmd = new_node;    // regardless, new_node is now the new newest_cmd
}

// this function allocates memory (if possible) to store previously entered commands
// if it is unable to provide the requested number of bytes, it returns NULL
// if able, it returns the memory address
static void* history_malloc(uint32_t byte_request)
{
  // this is block of memory reserved for history_malloc to divvy up
  static uint8_t __attribute__((aligned(4))) memory[HISTORY_SIZE];
  // we calculate and store the beginning and end of this memory
  static void *const memory_start = (void *)memory;
  static void *const memory_end = (void *)memory + HISTORY_SIZE;


  void *memory_piece_start; // this points to the start of a memory piece that is (hopefully) big enough
  void *memory_piece_end;   // this points to the end of that memory piece
  /*  next_piece_start points to the memory location immediately after the piece we just
      allocated this variable is static so it persists across function calls. This gives
      us a hint where to find the next available piece of memory */
  static void *next_piece_start;  

  // if nothing has been allocated yet, start at the beginning of memory
  if(newest_cmd == NULL){
    memory_piece_start = memory_start;
    memory_piece_end = memory_piece_start + byte_request;
    if(memory_piece_end <= memory_end){   // if we are not requesting more bytes than HISTORY_SIZE
      // the next block will probably start at the end of this one
      next_piece_start = memory_piece_end;
      return memory_piece_start;
    }else{
      // if we are requesting more bytes than HISTORY_SIZE, that's too big
      return NULL;
    }
  }else{    // memory has already been allocated
    memory_piece_start = next_piece_start;
    memory_piece_end = memory_piece_start + byte_request;
    if(newest_cmd >= oldest_cmd){
      if(memory_piece_end <= memory_end){   // if there is space after the newest_cmd and before memory_end
        next_piece_start = memory_piece_end;
        return memory_piece_start;
      }else if(memory_start + byte_request <= (void*)oldest_cmd){   //otherwise, check for space after memory_start and before oldest_cmd
        next_piece_start = memory_start + byte_request;
        return memory_start;
      }else{
        return NULL;        // otherwise, not enough memory
      }
    }else{  // newest_cmd < oldest_cmd
      // if the piece of memory we want to allocate does not overlap existing history commands
      if(memory_piece_end <= (void*)oldest_cmd){
        next_piece_start = memory_piece_end;
        return memory_piece_start;
      }else{
        return NULL;
      }
    }
  }
  // this should never be reached
  return NULL;
}

// this function frees the oldest command stored in history to make room for newer commands
static void free_oldest_cmd(void)
{
  // if the oldest_cmd exists
  if(oldest_cmd != NULL){
    oldest_cmd = oldest_cmd->prev;
    oldest_cmd->next = NULL;
  }
}

/***** Buffer Functions *****/
// this function pushes a byte of data into a ring buffer
// it returns 0 if successful, otherwise -1
static int32_t bufPush(ringBuf *buf, uint8_t value)
{
  // assuming the buffer size is a power of 2, 
  // modulus isn't needed to wrap the index
  uint32_t nextWI = (buf->writeIndex + 1) & (buf->size - 1);
  // as long as the next write index isn't the read index
  if(nextWI != buf->readIndex){
    // write the value and increment the write index
    buf->data[buf->writeIndex] = value;
    buf->writeIndex = nextWI;
    return (0);
  }else{  // if the next write index is the read index, buffer is full
    return(-1);
  }
}

// this function pops a byte of data out of the ring buffer
// if the buffer has data, it returns the value at the read index
// if the buffer is empty, it returns 0
static uint8_t bufPop(ringBuf *buf)
{
  // initialize the return value with a default value of 0
  uint8_t retval = 0;
  // if the read and write index don't match, the buffer contains data
  if(buf->readIndex != buf->writeIndex){
    // retrieve the data from the read index
    retval = buf->data[buf->readIndex];
    // increment the read index
    // assuming the buffer size is a power of 2, 
    // modulus isn't needed to wrap the index
    buf->readIndex = (buf->readIndex + 1) & (buf->size - 1);
  }
  // regardless of if the buffer is empty, return retval
  return(retval);
}

// this function returns true if the buffer is empty and false if not
static bool bufIsEmpty(ringBuf *buf)
{
  // buffer is empty if the read index matches the write index
  return((buf->readIndex) == (buf->writeIndex));
}




