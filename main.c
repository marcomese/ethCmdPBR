#include <sys/socket.h>
#include <netinet/in.h>
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
#define CONN_MAX_QUEUE   10

#define BIND_MAX_TRIES   10
#define LISTEN_MAX_TRIES 10

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

pthread_mutex_t mtx;

typedef struct cmdDecodeArgs{
    axiRegisters_t* regs;
    uint32_t*       cmdID;
    int             connfd;
    int*            socketStatus;
} cmdDecodeArgs_t;

typedef struct chkFifoArgs{
    axiRegisters_t* regs;
    uint32_t*       cmdID;
    int*            socketStatus;
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

void genFileName(uint32_t fileCounter, char* fileName, uint32_t fileNameLen){
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);

    snprintf(fileName, fileNameLen, "/srv/ftp/clkb_event_%04d%02d%02d%02d%02d%02d-%04d.dat.lock",
             ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
             ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
             fileCounter);

    return;
}

void unlockFile(char* fileName){
    char unlockedFileName[FILENAME_LEN] = "";

    if(strncmp(fileName,"",FILENAME_LEN) != 0){
        strncpy(unlockedFileName,fileName,strlen(fileName)-5);
        rename(fileName,unlockedFileName);
    }

    return;
}

void* cmdDecodeThread(void *arg){
    cmdDecodeArgs_t* cmdArg = (cmdDecodeArgs_t*)arg;
    const char *welcomeStr = "CLK BOARD\n";
    char rxBuf[CMD_MAX_LEN] = "";
    int  rxLen = 0;
    char chunk[CMD_MAX_LEN] = "";
    int localSocketStatus;

    write(cmdArg->connfd, welcomeStr, strlen(welcomeStr));

    while(*cmdArg->cmdID != EXIT){
        localSocketStatus = read(cmdArg->connfd, chunk, CMD_MAX_LEN - 1);

        pthread_mutex_lock(&mtx);
        *cmdArg->socketStatus = localSocketStatus;

        if(localSocketStatus <= 0){
            pthread_mutex_unlock(&mtx);
            pthread_exit(NULL);
        }

        for(int i = 0; i < localSocketStatus; i++){
            char ch = chunk[i];

            if(ch == '\n' || ch == '\r'){
                if(rxLen > 0){
                    rxBuf[rxLen] = '\0';
                    *cmdArg->cmdID = decodeCmdStr(cmdArg->regs, cmdArg->connfd, rxBuf, rxLen);
                    rxLen = 0;
                }
            }else if(rxLen < CMD_MAX_LEN - 1){
                rxBuf[rxLen++] = ch;
            }
        }

        pthread_mutex_unlock(&mtx);
    }

    pthread_exit((void *)cmdArg->cmdID);
}

void* checkFifoThread(void *arg){
    FILE *outFile;
    chkFifoArgs_t* chkArg = (chkFifoArgs_t*)arg;
    unsigned int exitCondition = 0;
    uint32_t statusReg = 0;
    uint32_t running = 0;
    int socketStatusLocal = 0;
    uint32_t cmdIDLocal = NONE;
    uint32_t eventCounter = 0;
    uint32_t fileCounter = 0;
    char fileName[FILENAME_LEN] = "";
    //pbrData_t data = {0, 0, 0, 0, 0, 0, 0, 0, "", 0};
    pbrData_t data = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    while(!exitCondition){
        dma_transfer_s2mm(chkArg->regs->dmaReg, DATA_BYTES, chkArg->socketStatus, chkArg->cmdID, chkArg->regs->statusReg, &mtx);

        pthread_mutex_lock(&mtx);
        socketStatusLocal = *chkArg->socketStatus;
        cmdIDLocal = *chkArg->cmdID;
        statusReg = *(chkArg->regs->statusReg);
        pthread_mutex_unlock(&mtx);

        exitCondition = (socketStatusLocal <= 0) || (cmdIDLocal == EXIT);

        running = statusReg & RUN_STATUS_MASK;

        //memset(data.gpsStr, '\0', DATA_GPS_BYTES);

        if(!exitCondition && running){
            if(!(eventCounter++ % TRG_NUM_PER_FILE)){
                unlockFile(fileName);
                genFileName(fileCounter++,fileName,FILENAME_LEN);
            }

            outFile = fopen(fileName, "ab");

            data.header    = DATA_HEADER;
            pthread_mutex_lock(&mtx);
            data.unixTime  = (uint32_t)time(NULL);
            data.evtCount  = *(chkArg->fifoData+EVTCNT_IDX);
            data.gtuCount  = *(chkArg->fifoData+GTUCNT_IDX);
            data.trgFlag   = *(chkArg->fifoData+TRGFLG_IDX);
            data.aliveTime = *(chkArg->fifoData+ALIVET_IDX);
            data.deadTime  = *(chkArg->fifoData+DEADT_IDX);
            data.status    = statusReg;

            pthread_mutex_unlock(&mtx);

            data.crc = crc_32((unsigned char *)&data, sizeof(data)-sizeof(data.crc), startCRC32);

            fwrite(&data, sizeof(data), 1, outFile);

            fclose(outFile);
        }else{
            eventCounter = 0;
            fileCounter = 0;
            unlockFile(fileName);
        }
    }

    pthread_exit((void *)chkArg->fifoData);
}

void* gpsCtrlThread(void* arg){

}

void* isrThread(void* arg){
    uint32_t count = 1;

    int fdNack = openUioByName("nack");
    if(fdNack < 0){
        fprintf(stderr,"Error in opening UIO for nack\n");
        pthread_exit(NULL);
    }
    int fdTrg = openUioByName("trig");
    if(fdTrg < 0){
        fprintf(stderr,"Error in opening UIO for trig\n");
        pthread_exit(NULL);
    }

    write(fdNack, &count, sizeof(count));
    write(fdTrg,  &count, sizeof(count));

    struct pollfd pfds[] = {
        { .fd = fdNack, .events = POLLIN },
        { .fd = fdTrg,  .events = POLLIN }
    };
    int nfds = sizeof(pfds)/sizeof(pfds[0]);

    while(1) {
        int ret = poll(pfds, nfds, -1);
        if (ret < 0) {
            fprintf(stderr,"Error in poll return value\n");
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (pfds[i].revents & POLLIN) {
                read(pfds[i].fd, &count, sizeof(count));
                if (pfds[i].fd == fdNack) {
                    printf("NACK ricevuto! count=%u\n", count);
                } else if (pfds[i].fd == fdTrg) {
                    printf("TRIG ricevuto! count=%u\n", count);
                }
                write(pfds[i].fd, &count, sizeof(count));
            }
        }
    }
    close(fdNack);
    close(fdTrg);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]){
    axiRegisters_t axiRegs;
    cmdDecodeArgs_t cmdDecodeArg;
    chkFifoArgs_t chkFifoArg;
    gpsCtrlArgs_t gpsArg;
    isrArgs_t isrArg;
    pthread_t cmdDecID;
    pthread_t chkSttID;
    pthread_t gpsCtrlID;
    pthread_t isrID;
    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;
    uint32_t* fifoData;
    uint32_t cmdDecRetVal  = 0;
    uint32_t chkSttRetVal  = 0;
    uint32_t gpsCtrlRetVal = 0;
    uint32_t isrRetVal = 0;
    uint32_t cmdID = NONE;
    int socketStatus = 1;
    int err = -1;
    int tries = 0;
    void* mmapRet = NULL;
    int fd   = 0;

    fd = openUioByName("AXIRegister@43c00000");
    if(fd < 0)
        fprintf(stderr,"Error in opening UIO for AXIRegister@43c00000 (commands register)\n");

    mmapRet = mmap(0, AXI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mmapRet == MAP_FAILED)
        fprintf(stderr,"Error in mapping CTRL_REG_ADDR\n");
    
    axiRegs.ctrlReg = (uint32_t*)mmapRet;

    fd = openUioByName("AXIStatusReg@43c10000");
    if(fd < 0)
        fprintf(stderr,"Error in opening UIO for AXIStatusReg@43c10000 (status and counters register)\n");

    mmapRet = mmap(0, AXI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mmapRet == MAP_FAILED)
        fprintf(stderr,"Error in mapping STATUS_REG_ADDR\n");

    axiRegs.statusReg = (uint32_t*)mmapRet;

    fd  = openUioByName("AXIStatusReg@43c20000");
    if(fd < 0)
        fprintf(stderr,"Error in opening UIO for AXIStatusReg@43c20000 (l1 counters register)\n");

    mmapRet = mmap(0, AXI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mmapRet == MAP_FAILED)
        fprintf(stderr,"Error in mapping L1CNT_REG_ADDR\n");

    axiRegs.l1CntReg = (uint32_t*)mmapRet;

    fd = openUioByName("dma@40400000");
    if(fd < 0)
        fprintf(stderr,"Error in opening UIO for dma@40400000 (DMA)\n");

    mmapRet = mmap(0, AXI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mmapRet == MAP_FAILED)
        fprintf(stderr,"Error in mapping DMA\n");

    axiRegs.dmaReg = (uint32_t*)mmapRet;

    fd = openUioByName("dma_buffer");
    if(fd < 0)
        fprintf(stderr,"Error in opening UIO for dma_buffer (DMA pool)\n");

    mmapRet = mmap(0, AXI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mmapRet == MAP_FAILED)
        fprintf(stderr,"Error in mapping DATA_ADDR\n");

    fifoData = (uint32_t*)mmapRet;

    printf("Initializing DMA...\n");
    dma_init_s2mm(axiRegs.dmaReg);
    dma_set_buffer(axiRegs.dmaReg, DATA_ADDR);
    printf("DMA Initialized!\n");

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(CONN_PORT);

    while(tries < BIND_MAX_TRIES){
        err = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if(err < 0){
            fprintf(stderr,"\tERR: Error in bind function: [%d]\nRetry %d...\n", err, tries);
            tries++;
        }else{
            printf("Bind OK\n");
            break;
        }
    }

    if(tries >= BIND_MAX_TRIES){
        fprintf(stderr,"Cannot bind to socket, program must be restarted\n");
        return -1;
    }

    tries = 0;

    while(tries < LISTEN_MAX_TRIES){
        err = listen(listenfd, CONN_MAX_QUEUE);
        if(err < 0){
            fprintf(stderr,"\tERR: Error in listen function: [%d]\nRetry %d...\n", err, tries);
            tries++;
        }else{
            printf("Listen OK\n");
            break;
        }
    }

    if(tries >= LISTEN_MAX_TRIES){
        fprintf(stderr,"Cannot listen to socket, program must be restarted\n");
        return -1;
    }

    cmdDecodeArg.regs         = &axiRegs;
    cmdDecodeArg.cmdID        = &cmdID;
    cmdDecodeArg.socketStatus = &socketStatus;

    chkFifoArg.regs         = &axiRegs;
    chkFifoArg.cmdID        = &cmdID;
    chkFifoArg.socketStatus = &socketStatus;
    chkFifoArg.fifoData     = fifoData;

    err = pthread_create(&isrID, NULL, &isrThread, (void*)&isrArg);
    if(err != 0){
        fprintf(stderr,"\tERR: Cannot create gpsCtrl thread, disconnecting...: [%s]\n", strerror(err));
    }

    while (1)
    {
        cmdID = NONE;
        socketStatus = 1;

        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if(connfd < 0){
            fprintf(stderr,"\tERR: Error in accept: [%s]\n", strerror(err));
            continue;
        }

        cmdDecodeArg.connfd = connfd;

        err = pthread_mutex_init(&mtx, NULL);
        if(err != 0){
            fprintf(stderr,"\nERR: Cannot init mutex, disconnecting...: [%s]\n", strerror(err));
            close(connfd);
            continue;
        }

        err = pthread_create(&cmdDecID, NULL, &cmdDecodeThread, (void*)&cmdDecodeArg);
        if(err != 0){
            fprintf(stderr,"\tERR: Cannot create cmdDecode thread, disconnecting...: [%s]\n", strerror(err));
            close(connfd);
            continue;
        }

        err = pthread_create(&chkSttID, NULL, &checkFifoThread, (void*)&chkFifoArg);
        if(err != 0){
            fprintf(stderr,"\tERR: Cannot create checkFifo thread, disconnecting...: [%s]\n", strerror(err));
            close(connfd);
            continue;
        }

        err = pthread_create(&gpsCtrlID, NULL, &gpsCtrlThread, (void*)&gpsArg);
        if(err != 0){
            fprintf(stderr,"\tERR: Cannot create gpsCtrl thread, disconnecting...: [%s]\n", strerror(err));
            close(connfd);
            continue;
        }

        pthread_join(cmdDecID,  (void**)&cmdDecRetVal);
        pthread_join(chkSttID,  (void**)&chkSttRetVal);
        pthread_join(gpsCtrlID, (void**)&gpsCtrlRetVal);

        pthread_mutex_destroy(&mtx);

        close(connfd);
    }

    pthread_join(isrID, (void**)&isrRetVal);
}
