#ifndef MAIN_H_
#define MAIN_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include <termios.h>
#include <poll.h>
#include "commands.h"
#include "registers.h"
#include "dma.h"
#include "crc32.h"

#define CONN_PORT        5000
#define CONN_MAX         5
#define CONN_MAX_QUEUE   10

#define BIND_MAX_TRIES   10
#define LISTEN_MAX_TRIES 10

#define TCP_KEEPALIVE_ON   1
#define TCP_KEEPIDLE_SEC   10
#define TCP_KEEPINTVL_SEC  5
#define TCP_KEEPCNT_PROBES 3

#define GPS_CONF_STR     0x02286D0200029903

#define DATA_HEADER      0x424B4C43
#define DATA_ADDR        0x1F000000
#define DATA_BYTES       24
#define DATA_NUMERICS    24
#define DATA_WORDS       (DATA_BYTES/4)
#define DATA_GPS_BYTES   (DATA_BYTES-(DATA_NUMERICS*4))

#define FILENAME_LEN     55
#define TRG_NUM_PER_FILE 25

#define EVTCNT_IDX 0
#define GTUCNT_IDX 1
#define TRGFLG_IDX 2
#define ALIVET_IDX 3
#define DEADT_IDX  4

#define RUN_STATUS_MASK 0x01

typedef struct cmdDecodeArgs{
    axiRegisters_t* regs;
    uint32_t*       cmdID;
    int             connfd;
    int             inUse;
} cmdDecodeArgs_t;

typedef struct chkFifoArgs{
    axiRegisters_t* regs;
    uint32_t*       cmdID;
    uint32_t*       fifoData;
} chkFifoArgs_t;

typedef struct gpsCtrlArgs{

} gpsCtrlArgs_t;

typedef struct isrArgs{
    uint32_t interruptID;
} isrArgs_t;

typedef struct pbrData{
    uint32_t     header;
    uint32_t     unixTime;
    uint32_t     evtCount;
    uint32_t     gtuCount;
    uint32_t     trgFlag;
    uint32_t     aliveTime;
    uint32_t     deadTime;
    uint32_t     status;
    //char         gpsStr[DATA_GPS_BYTES];
    unsigned int crc;
} pbrData_t;

typedef struct gpsData{

} gpsData_t;

#endif