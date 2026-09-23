#include "acquisition.h"
#include "imu.h"

static void genFileName(uint32_t fileCounter, uint8_t boardID, char* fileName, uint32_t fileNameLen){
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);

    snprintf(fileName, fileNameLen, "/srv/ftp/clk%d_event_%04d%02d%02d%02d%02d%02d-%04u.dat.lock",
             boardID,
             (ptm->tm_year + 1900) % 10000, (ptm->tm_mon + 1) % 100, ptm->tm_mday % 100,
             ptm->tm_hour % 100, ptm->tm_min % 100, ptm->tm_sec % 100,
             fileCounter % 10000);

    return;
}

static void unlockFile(char* fileName){
    char unlockedFileName[FILENAME_LEN] = "";

    if(strncmp(fileName,"",FILENAME_LEN) != 0){
        size_t base = strlen(fileName) - 5;
        memcpy(unlockedFileName, fileName, base);
        unlockedFileName[base] = '\0';
        rename(fileName, unlockedFileName);
    }

    return;
}

/* PL FIFO depth in 32 bit words (axis_data_fifo_0 FIFO_DEPTH in the block design) */
#define FIFO_DEPTH_WORDS 4096U
/* more records than the FIFO and the DMA store-and-forward buffer can hold */
#define PENDING_MAX      (FIFO_DEPTH_WORDS / DATA_PL_WORDS + 4U)

typedef struct {
    FILE*    file;
    char     name[FILENAME_LEN];
    uint32_t eventCounter;
    uint32_t fileCounter;
} outFile_t;

static void closeOutFile(outFile_t* out){
    if(out->file != NULL){
        fclose(out->file);
        out->file = NULL;
    }

    unlockFile(out->name);
    out->name[0] = '\0';
}

/* accepted triggers so far and words waiting in the PL FIFO */
static void readPlCounters(chkFifoArgs_t* chkArg, uint32_t* evt, uint32_t* words){
    pthread_mutex_lock(chkArg->mtx);
    *evt   = readReg(chkArg->regs->cntReg, CNT_REG_ADDR, EVT_COUNTER_ADDR);
    *words = readReg(chkArg->regs->aliveDeadReg, ALIVEDEAD_REG_ADDR, FIFO_COUNTER_ADDR);
    pthread_mutex_unlock(chkArg->mtx);
}

/* records waiting in the PL, in the FIFO or already in the DMA store-and-forward
 * buffer: accepted triggers minus the last event read. The FIFO occupancy alone
 * would miss the ones held by the DMA. If the event counter was reset during the
 * run (counter evt reset) the difference is meaningless: use the FIFO occupancy,
 * the event numbers read from the records bring lastEvt back in step */
static uint32_t pendingRecords(chkFifoArgs_t* chkArg, uint32_t lastEvt){
    uint32_t evt = 0, words = 0;
    uint32_t pending;

    readPlCounters(chkArg, &evt, &words);
    pending = evt - lastEvt;

    return (pending > PENDING_MAX) ? words / DATA_PL_WORDS : pending;
}

/* after a failed transfer the record it held is lost: count only what the FIFO still holds */
static uint32_t resyncLastEvt(chkFifoArgs_t* chkArg){
    uint32_t evt = 0, words = 0;

    readPlCounters(chkArg, &evt, &words);

    return evt - words / DATA_PL_WORDS;
}

/* builds the record from the DMA buffer and appends it to out (when open);
 * returns its event number */
static uint32_t writeRecord(chkFifoArgs_t* chkArg, uint32_t header, FILE* out){
    pbrData_t data = {0};

    data.header = header;
    pthread_mutex_lock(chkArg->mtx);
    data.unixTime    = (uint32_t)time(NULL);
    data.evtCount    = *(chkArg->fifoData+EVTCNT_IDX);
    data.gtuCount    = *(chkArg->fifoData+GTUCNT_IDX);
    data.ppsCount    = *(chkArg->fifoData+PPSCNT_IDX);
    data.clk40MCount = *(chkArg->fifoData+CLK40_IDX);
    data.trgFlag     = *(chkArg->fifoData+TRGFLG_IDX);
    data.aliveTime   = *(chkArg->fifoData+ALIVET_IDX);
    data.deadTime    = *(chkArg->fifoData+DEADT_IDX);
    data.statusLo    = *(chkArg->fifoData+STATUSLO_IDX);
    data.statusHi    = *(chkArg->fifoData+STATUSHI_IDX);
    memcpy(data.gpsStr, chkArg->gpsStr, DATA_GPS_BYTES);
    pthread_mutex_unlock(chkArg->mtx);

    if(chkArg->imuShared != NULL)
        imuGetSnapshot(chkArg->imuShared, chkArg->mtx, &data.imu);

    data.crc = crc_32((unsigned char *)&data, sizeof(data)-sizeof(data.crc), startCRC32);

    if(out != NULL){
        fwrite(&data, sizeof(data), 1, out);
        fflush(out);
    }

    return data.evtCount;
}

void* checkFifoThread(void *arg){
    chkFifoArgs_t* chkArg = (chkFifoArgs_t*)arg;
    /* boardID is set before this thread starts and never modified: no lock needed */
    const uint8_t  boardID = *(chkArg->boardID);
    const uint32_t header  = boardID ? ((boardID+0x30) << 24) | (DATA_HEADER & 0x00FFFFFF)
                                     : DATA_HEADER;
    uint32_t statusReg = 0;
    uint32_t running = 0;
    uint32_t wasRunning = 0;
    uint32_t lastEvt = 0;
    uint32_t count = 1;
    int imuRunning = 0;
    pthread_t imuTid;
    outFile_t out = {0};

    struct pollfd pfd = {
        .fd     = chkArg->fdTrg,
        .events = POLLIN
    };

    write(chkArg->fdTrg, &count, sizeof(count));

    while(1){
        int ret = poll(&pfd, 1, DMA_POLL_TIMEOUT_MS);

        if(ret < 0){
            if(errno == EINTR)
                continue;
            fprintf(stderr,"Error in poll for trig [%s]\n", strerror(errno));
            break;
        }

        pthread_mutex_lock(chkArg->mtx);
        statusReg  = *(chkArg->regs->statusReg);
        pthread_mutex_unlock(chkArg->mtx);

        running = statusReg & RUN_STATUS_MASK;

        if(wasRunning && !running)
            dma_reset_s2mm(chkArg->regs->dmaReg, DATA_ADDR);

        wasRunning = running;

        if(chkArg->imuArgs != NULL){
            if(running && !imuRunning){
                if(imuStart(chkArg->imuArgs, &imuTid) == 0)
                    imuRunning = 1;
            }else if(!running && imuRunning){
                imuStop(chkArg->imuArgs, imuTid);
                imuRunning = 0;
            }
        }

        /* re-arm the irq before draining: a trigger arriving meanwhile wakes us up again */
        if(ret > 0 && (pfd.revents & POLLIN)){
            read(chkArg->fdTrg, &count, sizeof(count));
            write(chkArg->fdTrg, &count, sizeof(count));
        }

        /* the trigger irq is an edge and the uio merges the ones arriving while we
         * are writing: drain every waiting record, also on the poll timeout */
        if(running){
            for(uint32_t n = pendingRecords(chkArg, lastEvt); n > 0; n--){
                if(dma_transfer_s2mm(chkArg->regs->dmaReg, DATA_PL_BYTES) != 0){
                    dma_reset_s2mm(chkArg->regs->dmaReg, DATA_ADDR);
                    lastEvt = resyncLastEvt(chkArg);
                    break;
                }

                if(!(out.eventCounter++ % TRG_NUM_PER_FILE)){
                    closeOutFile(&out);
                    genFileName(out.fileCounter++, boardID, out.name, FILENAME_LEN);
                    out.file = fopen(out.name, "ab");
                    if(out.file == NULL)
                        fprintf(stderr, "Error in opening file %s\n", out.name);
                }

                lastEvt = writeRecord(chkArg, header, out.file);
            }
        }else{
            out.eventCounter = 0;
            out.fileCounter  = 0;
            lastEvt          = 0;
            closeOutFile(&out);
        }
    }

    closeOutFile(&out);
    pthread_exit((void *)chkArg->fifoData);
}
