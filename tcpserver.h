#ifndef TCPSERVER_H_
#define TCPSERVER_H_
#include "main.h"

#define WSTR_HEAD "CLK BOARD SN"
#define WSTR_LEN  (sizeof(WSTR_HEAD) + 3)

cmdDecodeArgs_t* acquireSlot(cmdDecodeArgs_t* pool);
void             releaseSlot(cmdDecodeArgs_t* slot);
void*            cmdDecodeThread(void* arg);

#endif
