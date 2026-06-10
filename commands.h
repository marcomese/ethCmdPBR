#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <stdlib.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include "registers.h"

#define NONE              0x00000000
#define START_RUN         0xFFFFFFFF
#define STOP_RUN          0xAAAAAAAA
#define RELEASE_BUSY      0x55555555
#define SET_BUSY          0xCCCCCCCC
#define TRIGGER           0x33333333
#define RESET_GPS         0x66666666
#define CONFIGURE_GPS     0x99999999
#define GPS1_ON           0xF0F0F0F0
#define GPS2_ON           0x0F0F0F0F
#define RESET_GTU_COUNT   0x5A5A5A5A
#define RESET_L1_COUNT    0xA5A5A5A5
#define RESET_EVT_COUNT   0x3C3C3C3C
#define RESET_ALL_COUNT   0xC3C3C3C3
#define PPS_TRG_ON        0x96969696
#define PPS_TRG_OFF       0x69696969
#define MASK_EXT_TRG      0xFF00FF00
#define UNMASK_EXT_TRG    0x00FF00FF
#define SELF_TRG_ON       0x55AA55AA
#define SELF_TRG_OFF      0xAA55AA55
#define NO_ZYNQ0          0x33CC33CC
#define NO_ZYNQ1          0xCC33CC33
#define NO_ZYNQ2          0x99669966
#define NO_ZYNQ3          0x66996699
#define ZYNQ0_ON          0x0FF00FF0
#define ZYNQ1_ON          0xF00FF00F
#define ZYNQ2_ON          0xA55AA55A
#define ZYNQ3_ON          0x5AA55AA5
#define GPS1_NO           0xC33CC33C
#define GPS2_NO           0x3CC33CC3
#define GPS_AUTO_ON       0x69966996
#define GPS_AUTO_NO       0x96699669
#define READ_GTUCOUNTER   0xFFFF0000
#define READ_EVTCOUNTER   0x33CCCC33
#define READ_L10COUNTER   0xAAAA5555
#define READ_L11COUNTER   0x3333CCCC
#define READ_L12COUNTER   0xCCCC3333
#define READ_L13COUNTER   0x0F0FF0F0
#define READ_STATUS       0x00FFFF00
#define EXIT              0x0000FFFF
#define HELP              0x5555AAAA

#define CMD_MAX_LEN       15

#define DESC_MAX_LEN      50

#define STATUS_ID_MAX_LEN 128

#define TCP_SND_BUF       2048

struct cmd;
typedef void (*funcPtr_t)(axiRegisters_t* regDev, int connfd, struct cmd* cmd);

typedef struct cmd{
    const char *cmdStr;
    uint32_t cmdVal;
    const char *feedbackStr;
    funcPtr_t funcPtr;
    uint32_t baseAddr;
    uint32_t regAddr;
    const char *cmdDesc;
} cmd_t;

uint32_t decodeCmdStr(axiRegisters_t* regDev, int connfd, char* ethStr, int nBytes);

#endif
