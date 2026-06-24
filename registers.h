#ifndef REGISTERS_H_
#define REGISTERS_H_

#include <glob.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#define AXI_MAP_SIZE      65536UL

// Registers base addresses
#define CTRL_REG_ADDR     0x43C00000
#define STATUS_REG_ADDR   0x43C10000
#define L1CNT_REG_ADDR    0x43C20000
#define PPSADFL_REG_ADDR  0x43C30000
#define DMA_REG_ADDR      0x40400000

// Registers addresses
//  CTRL_REG registers
#define CMD_RECV_ADDR     0x43C00000
#define FIFO_STATUS_ADDR  0x43C00004

//  STATUS_REG registers
#define EVT_COUNTER_ADDR   0x43C10004
#define GTU_COUNTER_ADDR   0x43C10008
#define FIFO_COUNTER_ADDR  0x43C1000C

// L1CNT_REG_ADDR registers
#define L1_0_COUNTER_ADDR 0x43C20000
#define L1_1_COUNTER_ADDR 0x43C20004
#define L1_2_COUNTER_ADDR 0x43C20008
#define L1_3_COUNTER_ADDR 0x43C2000C

//PPS counter, Alive/Dead counter and TrgFlag registers
#define PPS_COUNTER_ADDR    0x43C30000
#define ALIVE_COUNTER_ADDR  0x43C30004
#define DEAD_COUNTER_ADDR   0x43C30008
#define TRGFLG_COUNTER_ADDR 0x43C3000C

uint32_t readReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr);
void writeReg(volatile uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr, uint32_t data);
int openUioByName(const char *name);

typedef struct axiRegisters{
    volatile uint32_t* ctrlReg;
    volatile uint32_t* statusReg;
    volatile uint32_t* l1CntReg;
    volatile uint32_t* ppsadflReg;
    volatile uint32_t* dmaReg;
} axiRegisters_t;

#endif