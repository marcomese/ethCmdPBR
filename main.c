#include "main.h"
#include "tcpserver.h"
#include "acquisition.h"
#include "gps.h"

int main(){
    const int keepalive = TCP_KEEPALIVE_ON;
    const int keepidle  = TCP_KEEPIDLE_SEC;
    const int keepintvl = TCP_KEEPINTVL_SEC;
    const int keepcnt   = TCP_KEEPCNT_PROBES;
    axiRegisters_t axiRegs;
    cmdDecodeArgs_t cmdDecodeArg[CONN_MAX];
    chkFifoArgs_t chkFifoArg;
    gpsCtrlArgs_t gpsArg[GPS_NUM];
    pthread_mutex_t poolMtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mtx     = PTHREAD_MUTEX_INITIALIZER;
    pthread_t chkSttID;
    pthread_t gpsCtrlID[GPS_NUM];
    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;
    uint32_t* fifoData;
    uint32_t cmdID = NONE;
    int err = -1;
    int tries = 0;
    void* mmapRet = NULL;
    int fd   = 0;
    char gpsStr[DATA_GPS_BYTES] = "";

    for(int i = 0; i < CONN_MAX; i++){
        cmdDecodeArg[i].regs    = &axiRegs;
        cmdDecodeArg[i].cmdID   = &cmdID;
        cmdDecodeArg[i].inUse   = 0;
        cmdDecodeArg[i].mtx     = &mtx;
        cmdDecodeArg[i].poolMtx = &poolMtx;
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


    chkFifoArg.regs     = &axiRegs;
    chkFifoArg.cmdID    = &cmdID;
    chkFifoArg.fifoData = fifoData;
    chkFifoArg.gpsStr   = gpsStr;
    chkFifoArg.fdTrg    = openUioByName("trig");
    chkFifoArg.mtx      = &mtx;

    if(chkFifoArg.fdTrg < 0)
        fprintf(stderr,"Error in opening UIO for trig\n");

    err = pthread_create(&chkSttID, NULL, &checkFifoThread, (void*)&chkFifoArg);
    if(err != 0){
        fprintf(stderr,"\tERR: Cannot create checkFifo thread, program must be restarted: [%s]\n", strerror(err));
        return -1;
    }

    for(int i = 0; i < GPS_NUM; i++){
        gpsArg[i].idx    = i;
        gpsArg[i].gpsStr = gpsStr + i*GPS_SLOT_LEN;
        gpsArg[i].mtx    = &mtx;

        err = pthread_create(&gpsCtrlID[i], NULL, &gpsCtrlThread, (void*)&gpsArg[i]);
        if(err != 0){
            fprintf(stderr,"\tERR: Cannot create gpsCtrl thread for GPS%d, program must be restarted: [%s]\n", i+1, strerror(err));
            return -1;
        }
    }

    while (1)
    {
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if(connfd < 0){
            fprintf(stderr,"\tERR: Error in accept: [%s]\n", strerror(errno));
            continue;
        }

        setsockopt(connfd,  SOL_SOCKET, SO_KEEPALIVE,  &keepalive, sizeof(keepalive));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPIDLE,  &keepidle,  sizeof(keepidle));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        setsockopt(connfd, IPPROTO_TCP, TCP_KEEPCNT,   &keepcnt,   sizeof(keepcnt));

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
}
