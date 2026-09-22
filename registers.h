#ifndef REGISTERS_H_
#define REGISTERS_H_

#include <glob.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#define AXI_MAP_SIZE      65536UL

// Registers base addresses (one AXI slave, one UIO device each)
#define CTRL_REG_ADDR       0x43C00000  // AXIRegister:  command and arguments
#define CNT_REG_ADDR        0x43C10000  // AXIStatusReg: evt, pps, gtu, clk40 counters
#define L1CNT_03_REG_ADDR   0x43C20000  // AXIStatusReg: L1 counters 0..3
#define ALIVEDEAD_REG_ADDR  0x43C30000  // AXIStatusReg: alive/dead time, master/slave, fifo count
#define L1CNT_46_REG_ADDR   0x43C40000  // AXIStatusReg: L1 counters 4..6, gtu/self trigger periods
#define STATUS_REG_ADDR     0x43C50000  // AXIStatusReg: status (64 bit), trg flags, fw sha
#define RATE_CLKTRG_REG_ADDR 0x43C60000 // AXIStatusReg: rates of ext clkb trg, trigger out, gtu, clk40
#define RATE_L103_REG_ADDR  0x43C70000  // AXIStatusReg: rates of L1 0..3
#define RATE_L146_REG_ADDR  0x43C80000  // AXIStatusReg: rates of L1 4..6, ext jtrg trg
#define DMA_REG_ADDR        0x40400000

// Registers addresses
//  CTRL_REG registers
#define CMD_RECV_ADDR       0x43C00000
#define CMD_ARG0_ADDR       0x43C00004
#define CMD_ARG1_ADDR       0x43C00008
#define CMD_ARG2_ADDR       0x43C0000C

//  CNT_REG registers
#define EVT_COUNTER_ADDR    0x43C10000
#define PPS_COUNTER_ADDR    0x43C10004
#define GTU_COUNTER_ADDR    0x43C10008
#define CLK40_COUNTER_ADDR  0x43C1000C

//  L1CNT_03_REG registers
#define L1_0_COUNTER_ADDR   0x43C20000
#define L1_1_COUNTER_ADDR   0x43C20004
#define L1_2_COUNTER_ADDR   0x43C20008
#define L1_3_COUNTER_ADDR   0x43C2000C

//  ALIVEDEAD_REG registers
#define ALIVE_COUNTER_ADDR  0x43C30000
#define DEAD_COUNTER_ADDR   0x43C30004
#define MASTERSLAVE_ADDR    0x43C30008  // "MSTR" or "SLV " (ASCII, MSB first) from command_decoder.vhd
#define FIFO_COUNTER_ADDR   0x43C3000C

#define MASTERSLAVE_MSTR    0x4D535452U
#define MASTERSLAVE_SLV     0x534C5620U

//  L1CNT_46_REG registers
#define L1_4_COUNTER_ADDR   0x43C40000
#define L1_5_COUNTER_ADDR   0x43C40004
#define L1_6_COUNTER_ADDR   0x43C40008
#define PERIODS_ADDR        0x43C4000C  // xlconcat: gtuPeriod | selfTrgScale | selfTrgPeriod

// PERIODS register layout (LSB first in the concat)
#define PRD_GTU_POS         0U          // gtu period in clk cycles, 16 bit
#define PRD_GTU_MASK        0xFFFFU
#define PRD_SELF_SCALE_POS  16U         // self trigger scale, 3 bit
#define PRD_SELF_SCALE_MASK 0x7U
#define PRD_SELF_COUNT_POS  19U         // self trigger count, 13 bit (0 = self trigger off)
#define PRD_SELF_COUNT_MASK 0x1FFFU

//  STATUS_REG registers
#define STATUS_LO_ADDR      0x43C50000  // status_register(31 downto 0)
#define STATUS_HI_ADDR      0x43C50004  // status_register(63 downto 32)
#define TRGFLG_ADDR         0x43C50008
#define FW_SHA_ADDR         0x43C5000C  // USR_ACCESS: git SHA embedded by Hog, 0 if not reproducible

// USR_ACCESS holds the 7 hex digit abbreviated SHA, zero extended to 32 bit
#define FW_SHA_MASK         0x0FFFFFFFU

//  RATE registers: edges counted in the last second by rateMeters (1 s gate on the 100 MHz clock)
#define RATE_EXTCLKB_ADDR   0x43C60000  // trigger from the other clock board (connector 7)
#define RATE_TRGOUT_ADDR    0x43C60004  // triggers accepted by the run control FSM
#define RATE_GTU_ADDR       0x43C60008  // selected GTU (internal or external)
#define RATE_CLK40_ADDR     0x43C6000C  // selected clk40M (internal or external), nominal 40000000
#define RATE_L1_0_ADDR      0x43C70000
#define RATE_L1_1_ADDR      0x43C70004
#define RATE_L1_2_ADDR      0x43C70008
#define RATE_L1_3_ADDR      0x43C7000C
#define RATE_L1_4_ADDR      0x43C80000
#define RATE_L1_5_ADDR      0x43C80004
#define RATE_L1_6_ADDR      0x43C80008
#define RATE_EXTJTRG_ADDR   0x43C8000C  // trigger from the JTRG connector

uint32_t readReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr);
void writeReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr, uint32_t data);
int openUioByName(const char *name);

typedef struct axiRegisters{
    volatile uint32_t* ctrlReg;
    volatile uint32_t* cntReg;
    volatile uint32_t* l1Cnt03Reg;
    volatile uint32_t* aliveDeadReg;
    volatile uint32_t* l1Cnt46Reg;
    volatile uint32_t* statusReg;
    volatile uint32_t* rateClkTrgReg;
    volatile uint32_t* rateL103Reg;
    volatile uint32_t* rateL146Reg;
    volatile uint32_t* dmaReg;
} axiRegisters_t;

volatile uint32_t* regionOf(axiRegisters_t* regDev, uint32_t baseAddr);

#endif
