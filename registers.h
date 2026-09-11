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
#define ALIVEDEAD_REG_ADDR  0x43C30000  // AXIStatusReg: alive/dead time, fifo count
#define L1CNT_47_REG_ADDR   0x43C40000  // AXIStatusReg: L1 counters 4..6
#define STATUS_REG_ADDR     0x43C50000  // AXIStatusReg: status (64 bit), trg flags, fw sha
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
#define FIFO_COUNTER_ADDR   0x43C3000C

//  L1CNT_47_REG registers
#define L1_4_COUNTER_ADDR   0x43C40000
#define L1_5_COUNTER_ADDR   0x43C40004
#define L1_6_COUNTER_ADDR   0x43C40008

//  STATUS_REG registers
#define STATUS_LO_ADDR      0x43C50000  // status_register(31 downto 0)
#define STATUS_HI_ADDR      0x43C50004  // status_register(63 downto 32)
#define TRGFLG_ADDR         0x43C50008
#define FW_SHA_ADDR         0x43C5000C  // USR_ACCESS: git SHA embedded by Hog, 0 if not reproducible

// USR_ACCESS holds the 7 hex digit abbreviated SHA, zero extended to 32 bit
#define FW_SHA_MASK         0x0FFFFFFFU

uint32_t readReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr);
void writeReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr, uint32_t data);
int openUioByName(const char *name);

typedef struct axiRegisters{
    volatile uint32_t* ctrlReg;
    volatile uint32_t* cntReg;
    volatile uint32_t* l1Cnt03Reg;
    volatile uint32_t* aliveDeadReg;
    volatile uint32_t* l1Cnt47Reg;
    volatile uint32_t* statusReg;
    volatile uint32_t* dmaReg;
} axiRegisters_t;

volatile uint32_t* regionOf(axiRegisters_t* regDev, uint32_t baseAddr);

#endif
