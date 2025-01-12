
#include "mcli.h"

#include <stdint.h>
#include <stdbool.h>

// RX_BUFFER_SIZE must be a power of 2
// this lets the read/write indices wrap much faster without using modulo
#define RX_BUFFER_SIZE    128

#define CMD_BUFFER_SIZE   128


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
  // cursorOffset is how far the cursor is from the end of the entered text
  uint32_t cursorOffset;
} txtBuf;


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
  .data = (char[CMD_BUFFER_SIZE]){0},
  .size = CMD_BUFFER_SIZE,
  .len = 0,
  .cursorOffset = 0
};


/* Internal Function Definitions */

extern int32_t putchar_(char c);

static uint8_t bufPop(ringBuf *buf);
static int32_t bufPush(ringBuf *buf, uint8_t value);
static bool bufIsEmpty(ringBuf *buf);

static inline bool isPrintableChar(char c);

static void handle_escape_char(char c);
static void handle_printable_char(char c);
static void handle_control_char(char c);

static void parse_command(void);



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
    if((previous_char[0] == '[') && (previous_char[1] == 0x1B)){
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
    // TODO
    rxBuffer.overflow = false;
  }
}

static void handle_printable_char(char c)
{
  // cmdBufRWSize is the largest size usable to hold printable characters
  // 2 bytes at the end are reserved for double '\0'
  static const uint32_t cmdBufRWSize = CMD_BUFFER_SIZE - 2;

  // if cmdBuffer is already holding the limit of printable characters
  if(cmdBuffer.len >= cmdBufRWSize){
    return;
  }

  uint32_t insert_pos = cmdBuffer.len - cmdBuffer.cursorOffset;

  

  putchar_(c);
}

static void handle_escape_char(char c)
{
  switch(c){
  case 'A':
    // cursor up
    // TODO
    break;
  case 'B':
    // cursor down
    // TODO
    break;
  case 'C':
    // cursor right
    // TODO
    break;
  case 'D':
    // cursor left
    // TODO
    break;
  default:
    // encountered unsupported character, do nothing
    break;
  }
}

static void handle_control_char(char c)
{
  switch(c){
  case '\n':
    if(previous_char[0] == '\r'){
      // newline was already handled from '\r'
      break;
    }// else fall through
  case '\r':
    // enter was pressed, handle it here!
    // TODO
    break;
  case 0x7F:
    // DEL case falls through and is treated the same as the BS case
  case '\b':
    // backspace was pressed, handle it here
    // TODO
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

static void parse_command(void)
{
  // if command is empty, return

  // otherwise, tokenize the command

  // look for a matching command name

  // and call it

}

/* Buffer Functions */

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




