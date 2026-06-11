#include "tcpserver.h"

cmdDecodeArgs_t* acquireSlot(cmdDecodeArgs_t* pool){
    cmdDecodeArgs_t* slot = NULL;

    pthread_mutex_lock(pool->poolMtx);
    for(int i = 0; i < CONN_MAX; i++){
        if(!pool[i].inUse){
            pool[i].inUse = 1;
            slot = &pool[i];
            break;
        }
    }
    pthread_mutex_unlock(pool->poolMtx);
    return slot;
}

void releaseSlot(cmdDecodeArgs_t* slot){
    pthread_mutex_lock(slot->poolMtx);
    slot->inUse = 0;
    pthread_mutex_unlock(slot->poolMtx);
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
                    pthread_mutex_lock(cmdArg->mtx);
                    uint32_t cmd = decodeCmdStr(cmdArg->regs, cmdArg->connfd, rxBuf, rxLen);
                    if(cmd != EXIT)
                        *cmdArg->cmdID = cmd;
                    pthread_mutex_unlock(cmdArg->mtx);
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
