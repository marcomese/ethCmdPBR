#include "main.h"

pthread_mutex_t poolMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx     = PTHREAD_MUTEX_INITIALIZER;

cmdDecodeArgs_t* acquireSlot(cmdDecodeArgs_t* pool){
    cmdDecodeArgs_t* slot = NULL;

    pthread_mutex_lock(&poolMtx);
    for(int i = 0; i < CONN_MAX; i++){
        if(!pool[i].inUse){
            pool[i].inUse = 1;
            slot = &pool[i];
            break;
        }
    }
    pthread_mutex_unlock(&poolMtx);
    return slot;
}

void releaseSlot(cmdDecodeArgs_t* slot){
    pthread_mutex_lock(&poolMtx);
    slot->inUse = 0;
    pthread_mutex_unlock(&poolMtx);
}

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
    int  localSocketStatus;
    int  exitConn = 0;

    write(cmdArg->connfd, welcomeStr, strlen(welcomeStr));

    while(!exitConn){
        localSocketStatus = read(cmdArg->connfd, chunk, CMD_MAX_LEN - 1);
        if(localSocketStatus <= 0)
            break;

        for(int i = 0; i < localSocketStatus; i++){
            char ch = chunk[i];
            if(ch == '\n' || ch == '\r'){
                if(rxLen > 0){
                    rxBuf[rxLen] = '\0';
                    pthread_mutex_lock(&mtx);
                    uint32_t cmd = decodeCmdStr(cmdArg->regs, cmdArg->connfd, rxBuf, rxLen);
                    if(cmd != EXIT)
                        *cmdArg->cmdID = cmd;
                    pthread_mutex_unlock(&mtx);
                    rxLen = 0;

                    if(cmd == EXIT){
                        exitConn = 1;
                        break;
                    }
                }
            }else if(rxLen < CMD_MAX_LEN - 1){
                rxBuf[rxLen++] = ch;
            }
        }
    }

    close(cmdArg->connfd);
    releaseSlot(cmdArg);
    pthread_exit(NULL);
}

void* checkFifoThread(void *arg){
    FILE *outFile;
    chkFifoArgs_t* chkArg = (chkFifoArgs_t*)arg;
    unsigned int exitCondition = 0;
    uint32_t statusReg = 0;
    uint32_t running = 0;
    uint32_t cmdIDLocal = NONE;
    uint32_t eventCounter = 0;
    uint32_t fileCounter = 0;
    char fileName[FILENAME_LEN] = "";
    //pbrData_t data = {0, 0, 0, 0, 0, 0, 0, 0, "", 0};
    pbrData_t data = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    while(!exitCondition){
        dma_transfer_s2mm(chkArg->regs->dmaReg, DATA_BYTES, chkArg->cmdID, chkArg->regs->statusReg, &mtx);

        pthread_mutex_lock(&mtx);
        cmdIDLocal = *chkArg->cmdID;
        statusReg = *(chkArg->regs->statusReg);
        pthread_mutex_unlock(&mtx);

        exitCondition = cmdIDLocal == EXIT;

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

    while(1){
        int ret = poll(pfds, nfds, -1);

        if(ret < 0){
            fprintf(stderr,"Error in poll return value\n");
            break;
        }

        for (int i = 0; i < nfds; i++){
            if(pfds[i].revents & POLLIN){
                read(pfds[i].fd, &count, sizeof(count));

                if(pfds[i].fd == fdNack){
                    fprintf(stderr,"PL command_decoder cannot decode command\n");
                }else if(pfds[i].fd == fdTrg){
                    printf("TRIG count=%u\n", count);
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
    const int keepalive = TCP_KEEPALIVE_ON;
    const int keepidle  = TCP_KEEPIDLE_SEC;
    const int keepintvl = TCP_KEEPINTVL_SEC;
    const int keepcnt   = TCP_KEEPCNT_PROBES;
    axiRegisters_t axiRegs;
    cmdDecodeArgs_t cmdDecodeArg[CONN_MAX];
    chkFifoArgs_t chkFifoArg;
    gpsCtrlArgs_t gpsArg;
    isrArgs_t isrArg;
    pthread_t chkSttID;
    pthread_t gpsCtrlID;
    pthread_t isrID;
    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;
    uint32_t* fifoData;
    uint32_t chkSttRetVal  = 0;
    uint32_t gpsCtrlRetVal = 0;
    uint32_t isrRetVal = 0;
    uint32_t cmdID = NONE;
    int err = -1;
    int tries = 0;
    void* mmapRet = NULL;
    int fd   = 0;

    for(int i = 0; i < CONN_MAX; i++){
        cmdDecodeArg[i].regs  = &axiRegs;
        cmdDecodeArg[i].cmdID = &cmdID;
        cmdDecodeArg[i].inUse = 0;
    }

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
            fprintf(stderr,"\tERR: Error in bind function: [%s]\nRetry %d...\n", strerror(errno), tries);
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
            fprintf(stderr,"\tERR: Error in listen function: [%s]\nRetry %d...\n", strerror(errno), tries);
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

    chkFifoArg.regs         = &axiRegs;
    chkFifoArg.cmdID        = &cmdID;
    chkFifoArg.fifoData     = fifoData;

    err = pthread_create(&chkSttID, NULL, &checkFifoThread, (void*)&chkFifoArg);
    if(err != 0){
        fprintf(stderr,"\tERR: Cannot create checkFifo thread, program must be restarted: [%s]\n", strerror(err));
        return -1;
    }

    err = pthread_create(&gpsCtrlID, NULL, &gpsCtrlThread, (void*)&gpsArg);
    if(err != 0){
        fprintf(stderr,"\tERR: Cannot create gpsCtrl thread, program must be restarted: [%s]\n", strerror(err));
        return -1;
    }

    err = pthread_create(&isrID, NULL, &isrThread, (void*)&isrArg);
    if(err != 0){
        fprintf(stderr,"\tERR: Cannot create isr thread, program must be restarted: [%s]\n", strerror(err));
        return -1;
    }

    while (1)
    {
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if(connfd < 0){
            fprintf(stderr,"\tERR: Error in accept: [%s]\n", strerror(errno));
            continue;
        }

        setsockopt(connfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

        cmdDecodeArgs_t* slot = acquireSlot(cmdDecodeArg);
        if(slot == NULL){
            const char *busyMsg = "ERROR: server full, try later\n";
            write(connfd, busyMsg, strlen(busyMsg));
            close(connfd);
            continue;
        }

        slot->connfd = connfd;

        pthread_t tid;
        err = pthread_create(&tid, NULL, &cmdDecodeThread, (void*)slot);
        if(err != 0){
            fprintf(stderr,"\tERR: Cannot create cmdDecode thread: [%s]\n", strerror(err));
            close(connfd);
            releaseSlot(slot);
            continue;
        }

        pthread_detach(tid);
    }
    pthread_mutex_destroy(&mtx);
    pthread_join(chkSttID,  (void**)&chkSttRetVal);
    pthread_join(gpsCtrlID, (void**)&gpsCtrlRetVal);
    pthread_join(isrID,     (void**)&isrRetVal);
}
