#include "acquisition.h"

static void genFileName(uint32_t fileCounter, char* fileName, uint32_t fileNameLen){
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);

    snprintf(fileName, fileNameLen, "/srv/ftp/clkb_event_%04d%02d%02d%02d%02d%02d-%04u.dat.lock",
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
    pbrData_t data = {0, 0, 0, 0, 0, 0, 0, 0, "", 0};

    while(!exitCondition){
        dma_transfer_s2mm(chkArg->regs->dmaReg, DATA_PL_BYTES, chkArg->cmdID, chkArg->regs->statusReg, chkArg->mtx);

        pthread_mutex_lock(chkArg->mtx);
        cmdIDLocal = *chkArg->cmdID;
        statusReg = *(chkArg->regs->statusReg);
        pthread_mutex_unlock(chkArg->mtx);

        exitCondition = cmdIDLocal == EXIT;

        running = statusReg & RUN_STATUS_MASK;

        memset(data.gpsStr, '\0', DATA_GPS_BYTES);

        if(!exitCondition && running){
            if(!(eventCounter++ % TRG_NUM_PER_FILE)){
                unlockFile(fileName);
                genFileName(fileCounter++,fileName,FILENAME_LEN);
            }

            outFile = fopen(fileName, "ab");

            data.header    = DATA_HEADER;
            pthread_mutex_lock(chkArg->mtx);
            data.unixTime  = (uint32_t)time(NULL);
            data.evtCount  = *(chkArg->fifoData+EVTCNT_IDX);
            data.gtuCount  = *(chkArg->fifoData+GTUCNT_IDX);
            data.trgFlag   = *(chkArg->fifoData+TRGFLG_IDX);
            data.aliveTime = *(chkArg->fifoData+ALIVET_IDX);
            data.deadTime  = *(chkArg->fifoData+DEADT_IDX);
            data.status    = statusReg;
            memcpy(data.gpsStr, chkArg->gpsStr, DATA_GPS_BYTES);
            pthread_mutex_unlock(chkArg->mtx);

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
