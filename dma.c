#include "dma.h"

unsigned int write_dma(volatile unsigned int *virtual_addr, int offset, unsigned int value){
    virtual_addr[offset >> 2] = value;
    return 0;
}

unsigned int read_dma(volatile unsigned int *virtual_addr, int offset){
    return virtual_addr[offset >> 2];
}

void dma_init_s2mm(volatile unsigned int *virtual_addr){
    write_dma(virtual_addr, S2MM_CONTROL_REGISTER, RESET_DMA);
    write_dma(virtual_addr, S2MM_CONTROL_REGISTER, HALT_DMA);
    write_dma(virtual_addr, S2MM_CONTROL_REGISTER, ENABLE_ALL_IRQ);
    return;
}

void dma_set_buffer(volatile unsigned int *virtual_addr, unsigned int dest_addr){
    write_dma(virtual_addr, S2MM_DST_ADDRESS_REGISTER, dest_addr);
    return;
}

int dma_s2mm_sync(volatile unsigned int *virtual_addr){
    unsigned int s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
    unsigned int retries = 0;

    while(!(s2mm_status & IOC_IRQ_FLAG) && !(s2mm_status & IDLE_FLAG)){
        if(s2mm_status & (STATUS_DMA_INTERNAL_ERR | STATUS_DMA_SLAVE_ERR | STATUS_DMA_DECODE_ERR)){
            fprintf(stderr,"DMA S2MM error, status=0x%08x\n", s2mm_status);
            return -1;
        }
/*
        if(++retries > DMA_SYNC_MAX_RETRIES){
            fprintf(stderr,"DMA S2MM sync timeout, status=0x%08x\n", s2mm_status);
            return -1;
        }
*/
        s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
    }

    write_dma(virtual_addr, S2MM_STATUS_REGISTER, IOC_IRQ_FLAG);

    return 0;
}

int dma_transfer_s2mm(volatile unsigned int *virtual_addr, unsigned int bytes_num){
    write_dma(virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
    write_dma(virtual_addr, S2MM_BUFF_LENGTH_REGISTER, bytes_num);

    return dma_s2mm_sync(virtual_addr);
}