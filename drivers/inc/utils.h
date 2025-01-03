#ifndef __UTILS_H
#define __UTILS_H

#include <stdint.h>

#define CHECK(x) do { \
  int32_t retval = (x); \
  if(retval < 0){ \
    return(retval); \
  } \
} while(0)

#endif /* __UTILS_H */
