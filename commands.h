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

/* PL command word, written in one shot to CMD_RECV_ADDR:
 *   bit 31..24 CMD (family), 23..16 ARG0 (action), 15..8 ARG1, 7..0 ARG2
 * Codes come from the [8,4,4] extended Hamming set (min distance 4), 0 is
 * reserved and decoded as NACK. Must match command_decoder.vhd. */
#define CMD_RUN     0x0FU
#define CMD_BSY     0x33U
#define CMD_TRG     0x55U
#define CMD_GPS     0x66U
#define CMD_PPS     0x99U
#define CMD_GTU     0xAAU
#define CMD_40M     0xCCU
#define CMD_CNT     0xF0U
#define CMD_CHN     0x3CU

#define ARG_ON      0x0FU   /* start, set, enable, on, internal, soft, configure, pps gps, counter l1, gps 1 */
#define ARG_OFF     0xF0U   /* stop, release, disable, off, external, pps clkb, counter evt, gps 2 */
#define ARG_PPS     0x33U   /* trg pps, pps auto, counter gtu, ch xgamma */
#define ARG_NORMAL  0xCCU   /* trg normal, counter all */
#define ARG_CLKB    0x55U   /* trg clkb */
#define ARG_SELF    0xAAU   /* trg self */
#define ARG_ALL     0xFFU   /* channel "all" */

#define PL_CMD(c, a0, a1, a2) \
    (((uint32_t)(c) << 24) | ((uint32_t)(a0) << 16) | ((uint32_t)(a1) << 8) | (uint32_t)(a2))

/* PL fabric clock (FCLK_CLK0): every ns -> cycles conversion and the self
 * trigger scale steps assume it, see selfTrigger.vhd */
#define PL_CLK_HZ            100000000UL
#define PL_CLK_NS            10UL

/* gtu internal <ns>: ARG1:ARG2 = period in clk cycles */
#define GTU_PERIOD_MIN_CYC   2UL
#define GTU_PERIOD_MAX_CYC   65535UL

/* trg self <ns>: ARG1:ARG2 = scale (bit 15..13) and count (bit 12..0) */
#define SELF_SCALE_NUM       6
#define SELF_COUNT_MAX       8191UL
#define SELF_SCALE_POS       13

/* return codes of decodeCmdStr(): a PL command word (never < 0x0F000000),
 * or one of these local ids */
#define CMD_ERROR            0x00000000U
#define CMD_LOCAL            0x00000001U
#define EXIT                 0x0000000AU

/* must match the generics of command_decoder in the block design */
#define EXTTRG_NUM           2

#define ZYNQ_NUM             7

#define PPS_NUM              3

#define CMD_MAX_LEN          64   /* longest line: "gps configure 0x02286D02 0x00029903" */

#define CMD_MAX_ARGS         4

#define STATUS_ID_MAX_LEN    128

#define STATUS_ID_STR_MAXLEN 16

#define TCP_SND_BUF          2048

uint32_t decodeCmdStr(axiRegisters_t* regDev, int connfd, char* ethStr, int nBytes);

#endif
