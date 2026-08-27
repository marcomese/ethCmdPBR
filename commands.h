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
#define CONFIGURE_GPS     0x99999999
#define GPS1_ON           0xF0F0F0F0
#define GPS2_ON           0x0F0F0F0F
#define CLKPPS_ON         0x33CCCC33
#define GPS1_NO           0xC33CC33C
#define GPS2_NO           0x3CC33CC3
#define CLKPPS_NO         0x3333CCCC
#define RESET_GTU_COUNT   0x5A5A5A5A
#define RESET_L1_COUNT    0xA5A5A5A5
#define RESET_EVT_COUNT   0x3C3C3C3C
#define RESET_ALL_COUNT   0xC3C3C3C3
#define PPS_TRG_ON        0x96969696
#define PPS_TRG_OFF       0x69696969
#define MASK_EXT_TRG0     0xFF00FF00
#define UNMASK_EXT_TRG0   0x00FF00FF
#define MASK_EXT_TRG1     0xFF0000FF
#define UNMASK_EXT_TRG1   0x00FFFF00
#define NO_ZYNQ0          0x33CC33CC
#define NO_ZYNQ1          0xCC33CC33
#define NO_ZYNQ2          0x99669966
#define NO_ZYNQ3          0x66996699
#define ZYNQ0_ON          0x0FF00FF0
#define ZYNQ1_ON          0xF00FF00F
#define ZYNQ2_ON          0xA55AA55A
#define ZYNQ3_ON          0x5AA55AA5
#define GPS_AUTO_ON       0x69966996
#define GPS_AUTO_NO       0x96699669
#define GTU_INT_ON        0xFFFF0000
#define GTU_INT_NO        0x0000FFFF
#define CLK40_INT_ON      0xAAAA5555
#define CLK40_INT_NO      0x5555AAAA
#define READ_GTUCOUNTER   0x00000009
#define READ_PPSCOUNTER   0x00000008
#define READ_EVTCOUNTER   0x00000007
#define READ_L10COUNTER   0x00000006
#define READ_L11COUNTER   0x00000005
#define READ_L12COUNTER   0x00000004
#define READ_L13COUNTER   0x00000003
#define READ_STATUS       0x00000002
#define HELP              0x00000001
#define EXIT              0x0000000A

// Local command ids (HELP, EXIT, READ_*) are never written to the PL command
// register: they live in the reserved window [1, LOCAL_CMD_MAX]. Every opcode
// sent to the PL must stay outside that window, otherwise decodeCmdStr() can
// return a value that tcpserver.c mistakes for EXIT.
#define LOCAL_CMD_MAX     0x000000FF

#define PL_OPCODE_CHECK(x) \
    _Static_assert((x) > LOCAL_CMD_MAX, #x " collides with the local command id window")

PL_OPCODE_CHECK(START_RUN);
PL_OPCODE_CHECK(STOP_RUN);
PL_OPCODE_CHECK(RELEASE_BUSY);
PL_OPCODE_CHECK(SET_BUSY);
PL_OPCODE_CHECK(TRIGGER);
PL_OPCODE_CHECK(CONFIGURE_GPS);
PL_OPCODE_CHECK(GPS1_ON);
PL_OPCODE_CHECK(GPS2_ON);
PL_OPCODE_CHECK(CLKPPS_ON);
PL_OPCODE_CHECK(GPS1_NO);
PL_OPCODE_CHECK(GPS2_NO);
PL_OPCODE_CHECK(CLKPPS_NO);
PL_OPCODE_CHECK(RESET_GTU_COUNT);
PL_OPCODE_CHECK(RESET_L1_COUNT);
PL_OPCODE_CHECK(RESET_EVT_COUNT);
PL_OPCODE_CHECK(RESET_ALL_COUNT);
PL_OPCODE_CHECK(PPS_TRG_ON);
PL_OPCODE_CHECK(PPS_TRG_OFF);
PL_OPCODE_CHECK(MASK_EXT_TRG0);
PL_OPCODE_CHECK(UNMASK_EXT_TRG0);
PL_OPCODE_CHECK(MASK_EXT_TRG1);
PL_OPCODE_CHECK(UNMASK_EXT_TRG1);
PL_OPCODE_CHECK(NO_ZYNQ0);
PL_OPCODE_CHECK(NO_ZYNQ1);
PL_OPCODE_CHECK(NO_ZYNQ2);
PL_OPCODE_CHECK(NO_ZYNQ3);
PL_OPCODE_CHECK(ZYNQ0_ON);
PL_OPCODE_CHECK(ZYNQ1_ON);
PL_OPCODE_CHECK(ZYNQ2_ON);
PL_OPCODE_CHECK(ZYNQ3_ON);
PL_OPCODE_CHECK(GPS_AUTO_ON);
PL_OPCODE_CHECK(GPS_AUTO_NO);
PL_OPCODE_CHECK(GTU_INT_ON);
PL_OPCODE_CHECK(GTU_INT_NO);
PL_OPCODE_CHECK(CLK40_INT_ON);
PL_OPCODE_CHECK(CLK40_INT_NO);

_Static_assert(EXIT <= LOCAL_CMD_MAX, "EXIT outside the local command id window");
_Static_assert(HELP <= LOCAL_CMD_MAX, "HELP outside the local command id window");
_Static_assert(READ_STATUS <= LOCAL_CMD_MAX, "READ_STATUS outside the local command id window");
_Static_assert(READ_GTUCOUNTER <= LOCAL_CMD_MAX, "READ_GTUCOUNTER outside the local command id window");

#define EXTTRG_NUM           2

#define ZYNQ_NUM             4

#define PPS_NUM              3

#define CMD_MAX_LEN          15

#define DESC_MAX_LEN         50

#define STATUS_ID_MAX_LEN    128

#define STATUS_ID_STR_MAXLEN 16

#define TCP_SND_BUF          2048

struct cmd;
typedef void (*funcPtr_t)(axiRegisters_t* regDev, int connfd, struct cmd* cmd);

typedef struct cmd{
    const char* cmdStr;
    uint32_t    cmdVal;
    const char* feedbackStr;
    funcPtr_t   funcPtr;
    uint32_t    baseAddr;
    uint32_t    regAddr;
    const char* cmdDesc;
} cmd_t;

uint8_t sortCmd(void);

uint32_t decodeCmdStr(axiRegisters_t* regDev, int connfd, char* ethStr, int nBytes);

#endif
