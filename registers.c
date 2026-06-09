#include "registers.h"

static uint32_t getOffset(uint32_t baseAddr, uint32_t regAddr){
    return ((regAddr - baseAddr) >> 2);
}

int openUioByName(const char *name) {
    glob_t g;
    if (glob("/sys/class/uio/uio*", 0, NULL, &g) != 0) {
        return -1;
    }

    static const char *subpaths[] = {
        "maps/map0/name",
        "name",
        NULL
    };

    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *uioPath = g.gl_pathv[i];

        for (int j = 0; subpaths[j] != NULL; j++) {
            char path[128] = "";
            char buf[64] = "";

            snprintf(path, sizeof(path), "%s/%s", uioPath, subpaths[j]);

            FILE *f = fopen(path, "r");
            if (!f) continue;

            char *r = fgets(buf, sizeof(buf), f);

            fclose(f);

            if (!r) continue;

            buf[strcspn(buf, "\n")] = '\0';

            if (strcmp(buf, name) == 0) {
                char dev[64] = "";
                const char *base = strrchr(uioPath, '/');

                if (!base) continue;

                snprintf(dev, sizeof(dev), "/dev%s", base);

                int fd = open(dev, O_RDWR);

                globfree(&g);

                return fd;
            }
        }
    }
    globfree(&g);
    return -1;
}

uint32_t readReg(uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr){
    uint32_t offset = getOffset(baseAddr, regAddr);
    return *(devAddr + offset);
}

void writeReg(uint32_t* devAddr, uint32_t baseAddr, uint32_t regAddr, uint32_t data){
    uint32_t offset = getOffset(baseAddr, regAddr);
    *(devAddr + offset) = data;
    msync(devAddr,  AXI_MAP_SIZE, MS_SYNC); 
}
