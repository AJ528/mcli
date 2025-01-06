
#include "mcli.h"

#include <stdint.h>
#include <stdbool.h>

// RX_BUFFER_SIZE must be a power of 2
// this lets the indices wrap much faster without using modulo
#define RX_BUFFER_SIZE    128


typedef struct {
  uint8_t *data;
  uint32_t size;
  uint32_t writeIndex;
  uint32_t readIndex;
} ringBuf;

static ringBuf rxBuffer = {
  .data = (uint8_t[RX_BUFFER_SIZE]){},
  .size = RX_BUFFER_SIZE,
  .writeIndex = 0,
  .readIndex = 0
};

static bool rxBuffer_overflow = false;

extern int32_t putchar_(char c);

static uint8_t bufPop(ringBuf *buf);
static int32_t bufPush(ringBuf *buf, uint8_t value);
static bool bufIsEmpty(ringBuf *buf);


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

void cli_input(char c)
{
  int32_t result = bufPush(&rxBuffer, (uint8_t)c);
  if(result < 0){
    rxBuffer_overflow = true;
  }
}

void cli_process(void)
{
  while(!bufIsEmpty(&rxBuffer)){
    char c = (char)bufPop(&rxBuffer);
    putchar_(c);
  }
}











