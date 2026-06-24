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
#include <sys/eventfd.h>
#include "commands.h"
#include "registers.h"
#include "dma.h"
#include "crc32.h"

#define CONN_PORT          5000
#define CONN_MAX           5
#define CONN_MAX_QUEUE     10
#define BIND_MAX_TRIES     10
#define LISTEN_MAX_TRIES   10
#define TCP_KEEPALIVE_ON   1
#define TCP_KEEPIDLE_SEC   10
#define TCP_KEEPINTVL_SEC  5
#define TCP_KEEPCNT_PROBES 3

#define GPS_NUM        2
#define GPS_DEV_BASE   "/dev/ttyUL"
#define GPS_DEV_LEN    (sizeof(GPS_DEV_BASE) + 4)
#define GPS_TOK        "$PTNL"
#define GPS_TOK_LEN    5
#define GPS_LINE_LEN   128
#define GPS_HEADER     "GPS"
#define GPS_HEADER_LEN 8

#define DATA_HEADER    0x424B4C43
#define DATA_ADDR      0x1F000000
#define DATA_PL_BYTES  24
#define GPS_SLOT_LEN   244
#define DATA_GPS_BYTES (GPS_NUM * GPS_SLOT_LEN)
#define DATA_BYTES     (DATA_PL_BYTES + DATA_GPS_BYTES)

#define FILENAME_LEN     64

#define TRG_NUM_PER_FILE 25

#define EVTCNT_IDX 5
#define GTUCNT_IDX 4
#define TRGFLG_IDX 3
#define ALIVET_IDX 2
#define DEADT_IDX  1
#define STATUS_IDX 0

#define RUN_STATUS_MASK 0x01

typedef struct cmdDecodeArgs{
    axiRegisters_t*  regs;
    uint32_t*        cmdID;
    int              connfd;
    int              inUse;
    pthread_mutex_t* mtx;
    pthread_mutex_t* poolMtx;
} cmdDecodeArgs_t;

typedef struct chkFifoArgs{
    axiRegisters_t*  regs;
    uint32_t*        cmdID;
    volatile uint32_t* fifoData;
    char*            gpsStr;
    int              fdTrg;
    pthread_mutex_t* mtx;
} chkFifoArgs_t;

typedef struct gpsCtrlArgs{
    int              idx;
    char*            gpsStr;
    int              cfgIrq;
    pthread_mutex_t* mtx;
} gpsCtrlArgs_t;

typedef struct gpsCfgIrqArgs{
    int              fdCfgIrq;
    int*             cfgIrqs;
    pthread_mutex_t* mtx;
} gpsCfgIrqArgs_t;

typedef struct pbrData{
    uint32_t     header;
    uint32_t     unixTime;
    uint32_t     evtCount;
    uint32_t     gtuCount;
    uint32_t     trgFlag;
    uint32_t     aliveTime;
    uint32_t     deadTime;
    uint32_t     status;
    char         gpsStr[DATA_GPS_BYTES];
    unsigned int crc;
} pbrData_t;

#endif
