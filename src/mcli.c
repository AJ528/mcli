
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

/* Internal Structures */

typedef struct {
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

/* Internal Variables and Structures */

// ring buffer to hold received characters until they are processed
static ringBuf rxBuffer = {
  .data = (uint8_t[RX_BUFFER_SIZE]){},
  .size = RX_BUFFER_SIZE,
  .writeIndex = 0,
  .readIndex = 0,
  .overflow = false
};

static char previous_char[2] = {0};

static txtBuf cmdBuffer = {
  .size = CMD_BUFFER_SIZE,
  .len = 0,
  .cursorOffset = 0,
  .data = (char[CMD_BUFFER_SIZE]){0},
};

// newest command stored in history
static cmdHistory *newest_cmd = NULL;
// oldest command stored in history
static cmdHistory *oldest_cmd = NULL;
// history command currently being shown
static cmdHistory *history_cmd = NULL;


/* Internal Function Definitions */

extern int32_t putchar_(char c);

static uint8_t bufPop(ringBuf *buf);
static int32_t bufPush(ringBuf *buf, uint8_t value);
static bool bufIsEmpty(ringBuf *buf);

static void history_display(cmdHistory *hist_cmd);
static void history_input(txtBuf *cmd);
static void * history_malloc(uint32_t byte_request);
static void free_oldest_cmd(void);

static inline bool isPrintableChar(char c);
static inline bool isBlank(txtBuf *buf);
static inline void reset_cmdBuffer(void);
static inline void print_prompt(void);
static void clear_cmd_line(bool show_prompt);

static void handle_escape_char(char c);
static void handle_printable_char(char c);
static void handle_control_char(char c);

static void parse_command(void);
static int32_t tokenize_command(char* cmd_buffer, uint32_t* argc, char* argv[]);



void cli_input(char c)
{
  int32_t result = bufPush(&rxBuffer, (uint8_t)c);
  if(result < 0){
    rxBuffer.overflow = true;
  }
}

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

  uint32_t insert_pos = cmdBuffer.len - cmdBuffer.cursorOffset;
  memmove_(&(cmdBuffer.data[insert_pos+1]), &(cmdBuffer.data[insert_pos]), cmdBuffer.cursorOffset + 1);
  cmdBuffer.len++;
  cmdBuffer.data[insert_pos] = c;

  if(cmdBuffer.cursorOffset > 0){
    puts_(esc_seq_insert_char);
  } 

  putchar_(c);
}

static void handle_escape_char(char c)
{
  // this escape sequence moves the cursor 1 space to the right
  static const char esc_seq_cursor_right[] = "\x1B[C";
  // this escape sequence moves the cursor 1 space to the left
  static const char esc_seq_cursor_left[] = "\x1B[D";

  switch(c){
  case 'A':
    // cursor up
    if(history_cmd == NULL){
      history_cmd = newest_cmd;
    }else if(history_cmd->next != NULL){
      history_cmd = history_cmd->next;
    }
    history_display(history_cmd);
    break;
  case 'B':
    // cursor down
    if(history_cmd != NULL){
      history_cmd = history_cmd->prev;
    }
    history_display(history_cmd);
    break;
  case 'C':
    // cursor right
    if(cmdBuffer.cursorOffset > 0){
      cmdBuffer.cursorOffset--;
      puts_(esc_seq_cursor_right);
    }
    break;
  case 'D':
    // cursor left
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
    history_input(&cmdBuffer);
    history_cmd = NULL;
    parse_command();
    // reset the command buffer
    reset_cmdBuffer();
    print_newline();
    print_prompt();
    break;
  case 0x7F:
    // DEL case falls through and is treated the same as the BS case
  case '\b':
    // backspace was pressed, handle it here
    puts_(esc_seq_backspace);
    uint32_t insert_pos = cmdBuffer.len - cmdBuffer.cursorOffset;
    memmove_(&(cmdBuffer.data[insert_pos-1]), &(cmdBuffer.data[insert_pos]), cmdBuffer.cursorOffset + 1);
    cmdBuffer.len--;
    break;
  default:
    // encountered unsupported character, do nothing
    break;
  }
}

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

static inline bool isBlank(txtBuf *buf)
{
  for(uint32_t i = 0; i < buf->len; i++){
    if(isPrintableChar(buf->data[i])){
      return false;
    }
  }
  return true;
}

static inline void reset_cmdBuffer(void)
{
    cmdBuffer.data[0] = '\0';
    cmdBuffer.len = 0;
    cmdBuffer.cursorOffset = 0;
}

static void clear_cmd_line(bool show_prompt)
{
  // clears the entire line, then resets the cursor to the beginning of the line
  static const char esc_seq_clear_line[] = "\x1B[2K\r";
  puts_(esc_seq_clear_line);
  if(show_prompt){
    print_prompt();
  }
}

static inline void print_prompt(void)
{
  puts_("# ");
}

static void parse_command(void)
{
  uint32_t argc;
  char* argv[MAX_NUM_ARGS + 1];

  int32_t retval = tokenize_command(cmdBuffer.data, &argc, argv);

  if(retval < 0){
    return;
  }

  // look for a matching command name
  // and call it
}

static int32_t tokenize_command(char* cmd_buffer, uint32_t* argc, char* argv[])
{
  uint32_t i = 0;
  char previous_c = ' ';
  char c = cmd_buffer[i];

  *argc = 0;

  while(c != '\0'){
    if((c != ' ') && (previous_c == ' ')){
      if((*argc) > MAX_NUM_ARGS){
        println_("ERROR: too many arguments passed");
        return (-1);
      }else{
        argv[(*argc)] = &(cmd_buffer[i]);
        (*argc)++;
      }
    }else if(c == ' '){
      cmd_buffer[i] = '\0';
    }
    previous_c = c;
    i++;
    c = cmd_buffer[i];
  }
  return 0;
}

/***** History Functions *****/
static void history_display(cmdHistory *hist_cmd)
{
  reset_cmdBuffer();
  clear_cmd_line(true);
  if(hist_cmd != NULL){
    memcpy_(cmdBuffer.data, &hist_cmd->cmd, (hist_cmd->cmd_len+1));
    cmdBuffer.len = hist_cmd->cmd_len;
    puts_(cmdBuffer.data);
  }
}
static void history_input(txtBuf *cmd)
{
  // copy the command into history if it doesn't match the newest history command

  if(newest_cmd != NULL){
    if(strcmp_(&newest_cmd->cmd, cmd->data) == 0){  // if the command we want to put into history is already newest_cmd, skip
      return;
    }
  }

  if(isBlank(cmd) == true){   // if the cmd is blank, don't store it
    return;
  }



  uint32_t size_req = (sizeof(cmdHistory)-3) + (cmd->len);

  // make sure we request bytes in word increments
  while(size_req & 0x03){
    size_req++;
  }

  cmdHistory *new_node = (cmdHistory *)history_malloc(size_req);

  while(new_node == NULL){
    if(oldest_cmd == NULL){
      return;   // no room to store the command
    }else{
      // free the oldest entry and retry
      free_oldest_cmd();
      new_node = (cmdHistory *)history_malloc(size_req);
    }
  }

  new_node->cmd_len = cmd->len;
  new_node->prev = NULL;
  new_node->next = newest_cmd;
  memcpy_(&new_node->cmd, cmd->data, cmd->len);
  *((char*)&new_node->cmd + cmd->len) = '\0';   // put a null byte on the end of the string

  if(newest_cmd == NULL){   // if there aren't any commands in the history yet
    oldest_cmd = new_node;  // this entry automatically becomes the oldest
  }else{
    newest_cmd->prev = new_node;  // otherwise, newest_command now has a younger brother
  }

  newest_cmd = new_node;    // regardless, new_node is now the new newest_command
}

static void * history_malloc(uint32_t byte_request)
{
  // allocate memory to store a new history command
  static uint8_t __attribute__((aligned(4))) memory[HISTORY_SIZE];
  static void *const memory_start = (void *)memory;
  static void *const memory_end = (void *)memory + HISTORY_SIZE;

  void *memory_block_start;
  void *memory_block_end;
  static void *next_block;

  // if nothing has been allocated yet, start at the beginning
  if(newest_cmd == NULL){
    memory_block_start = memory_start;
    memory_block_end = memory_block_start + byte_request;
    if(memory_block_end <= memory_end){   // check if we are requesting more bytes than HISTORY_SIZE
      next_block = memory_block_end;
      return memory_block_start;
    }else{
      return NULL;
    }
  }else{    // memory has already been allocated
    memory_block_start = next_block;
    memory_block_end = memory_block_start + byte_request;
    if(newest_cmd >= oldest_cmd){
      if(memory_block_end <= memory_end){   // if there is space after the newest_cmd and before memory_end
        next_block = memory_block_end;
        return memory_block_start;
      }else if(memory_start + byte_request <= (void*)oldest_cmd){   //otherwise, check for space after memory_start and before oldest_cmd
        next_block = memory_start + byte_request;
        return memory_start;
      }else{
        return NULL;        // otherwise, not enough memory
      }
    }else{
      if(memory_block_end <= (void*)oldest_cmd){
        next_block = memory_block_end;
        return memory_block_start;
      }else{
        return NULL;
      }
    }
  }

  return NULL;
}

static void free_oldest_cmd(void)
{
  if(oldest_cmd != NULL){
    oldest_cmd = oldest_cmd->prev;
    oldest_cmd->next = NULL;
  }
}

/***** Buffer Functions *****/

static int32_t bufPush(ringBuf *buf, uint8_t value)
{
  // assuming the buffer size is a power of 2, 
  // modulus isn't needed to wrap the index
  uint32_t nextWI = (buf->writeIndex + 1) & (buf->size - 1);
  if(nextWI != buf->readIndex){
    buf->data[buf->writeIndex] = value;
    buf->writeIndex = nextWI;
    return (0);
  }else{
    return(-1);
  }
}

static uint8_t bufPop(ringBuf *buf)
{
  uint8_t retval = 0;
  if(buf->readIndex != buf->writeIndex){
    retval = buf->data[buf->readIndex];
    // assuming the buffer size is a power of 2, 
    // modulus isn't needed to wrap the index
    buf->readIndex = (buf->readIndex + 1) & (buf->size - 1);
  }
  return(retval);
}

static bool bufIsEmpty(ringBuf *buf)
{
  return((buf->readIndex) == (buf->writeIndex));
}




