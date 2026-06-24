#include "gps.h"

const uint8_t gpsConfStr[] = {0x02, 0x28, 0x6D, 0x02, 0x00, 0x02, 0x99, 0x03};

void* gpsCfgIrqThread(void* arg){
    gpsCfgIrqArgs_t* cfgIrqArg = (gpsCfgIrqArgs_t*)arg;
    uint32_t count = 1;

    struct pollfd pfd = {
        .fd     = cfgIrqArg->fdCfgIrq,
        .events = POLLIN
    };

    write(cfgIrqArg->fdCfgIrq, &count, sizeof(count));

    while(1){
        int ret = poll(&pfd, 1, -1);
        if(ret < 0){
            if(errno == EINTR)
                continue;
            fprintf(stderr,"Error in poll [%s]\n", strerror(errno));
            break;
        }

        if(!(pfd.revents & POLLIN))
            continue;
///////
        uint32_t irqCount = 0;
        if(read(cfgIrqArg->fdCfgIrq, &irqCount, sizeof(irqCount)) == (ssize_t)sizeof(irqCount)){
            uint64_t one = 1;

            for(int i = 0; i < GPS_NUM; i++){
                if(write(cfgIrqArg->cfgIrqs[i], &one, sizeof(one)) != (ssize_t)sizeof(one))
                    fprintf(stderr,"Warning: GPS%d notify failed\n", i);
            }
        }

        if(write(cfgIrqArg->fdCfgIrq, &count, sizeof(count)) != (ssize_t)sizeof(count))
            fprintf(stderr,"Warning: GPS configure irq not re-armed\n");
///////
    }

    pthread_exit(NULL);
}

void* gpsCtrlThread(void* arg){
    gpsCtrlArgs_t* gpsArg = (gpsCtrlArgs_t*)arg;
    char ttyDev[GPS_DEV_LEN];
    char block[GPS_SLOT_LEN];
    int  blockLen  = 0;
    int  blockOpen = 0;
    struct pollfd pfds[2];

    unsigned idx = (unsigned)(gpsArg->idx + 1) % 100; // for avoiding warnings on unsigned size in snprintf
    snprintf(ttyDev, GPS_DEV_LEN, "%s%u", GPS_DEV_BASE, idx);

    int fd = open(ttyDev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0){
        fprintf(stderr,"Error opening %s\n", ttyDev);
        pthread_exit(NULL);
    }

    struct termios tty;
    tcgetattr(fd, &tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_lflag |= ICANON;
    tty.c_lflag &= ~(ECHO | ECHOE | ECHOK | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag |= ICRNL;
    tty.c_iflag &= ~(INLCR | IGNCR);
    tty.c_oflag &= ~OPOST;
    tcsetattr(fd, TCSANOW, &tty);

    pfds[0].fd     = fd;
    pfds[0].events = POLLIN;
    pfds[1].fd     = gpsArg->cfgIrq;
    pfds[1].events = POLLIN;

    while(1){
        int ret = poll(pfds, 2, -1);

        if(ret < 0){
            if(errno == EINTR)
                continue;
            fprintf(stderr,"Error in poll [%s]\n", strerror(errno));
            break;
        }

        if(pfds[0].revents & POLLIN){
            while(1){
                char line[GPS_LINE_LEN];
                int  n = read(fd, line, GPS_LINE_LEN - 1);

                if(n <= 0)
                    break;

                line[n] = '\0';

                if(n == 1 && line[0] == '\n')
                    continue;

                if(strncmp(line, GPS_TOK, GPS_TOK_LEN) == 0){
                    if(blockOpen && blockLen > 0){
                        pthread_mutex_lock(gpsArg->mtx);
                        memset(gpsArg->gpsStr, '\0', GPS_SLOT_LEN);
                        memcpy(gpsArg->gpsStr, block, blockLen);
                        pthread_mutex_unlock(gpsArg->mtx);
                    }
                    int hlen = snprintf(block, GPS_HEADER_LEN, "%s%d", GPS_HEADER, idx);
                    blockLen  = hlen;
                    blockOpen = 1;
                }

                if(!blockOpen)
                    continue;

                if(blockLen + n < GPS_SLOT_LEN){
                    memcpy(block + blockLen, line, n);
                    blockLen += n;
                }else{
                    blockLen  = 0;
                    blockOpen = 0;
                }
            }
        }

        if(pfds[1].revents & POLLIN){
            uint64_t ev = 0;

            if(read(gpsArg->cfgIrq, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)){
                ssize_t n = write(fd, gpsConfStr, sizeof(gpsConfStr));

                if(n != (ssize_t)sizeof(gpsConfStr))
                    fprintf(stderr,"Error: GPS%d configuration not written!\n", idx);
            }
        }
    }

    close(fd);
    pthread_exit(NULL);
}
