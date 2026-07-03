#ifndef TCPSERVER_H_
#define TCPSERVER_H_
#include "main.h"

#define WSTR_LEN 15

cmdDecodeArgs_t* acquireSlot(cmdDecodeArgs_t* pool);
void             releaseSlot(cmdDecodeArgs_t* slot);
void*            cmdDecodeThread(void* arg);

#endif
