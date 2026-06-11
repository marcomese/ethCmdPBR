#include "gps.h"

void* gpsCtrlThread(void* arg){
    gpsCtrlArgs_t* gpsArg = (gpsCtrlArgs_t*)arg;
    char ttyDev[GPS_DEV_LEN];
    char block[GPS_SLOT_LEN];
    int  blockLen  = 0;
    int  blockOpen = 0;
    struct pollfd pfd;

    unsigned idx = (unsigned)(gpsArg->idx + 1) % 100;
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

    pfd.fd     = fd;
    pfd.events = POLLIN;

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
                int hlen = snprintf(block, GPS_HEADER_LEN, "%s%d", GPS_HEADER, gpsArg->idx+1);
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

    close(fd);
    pthread_exit(NULL);
}
