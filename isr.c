#include "isr.h"

void* isrThread(void* arg){
    (void)arg;

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
        {.fd = fdNack, .events = POLLIN},
        {.fd = fdTrg,  .events = POLLIN}
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
