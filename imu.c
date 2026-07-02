#include "imu.h"

typedef struct {
    float accel;
    float magn;
    float anglvel;
    float rot;
} imuScales_t;

static char* getIMUPathByName(const char* name){
    glob_t g;
    char* result = NULL;

    int ret = glob("/sys/bus/iio/devices/iio:device*", 0, NULL, &g);
    if(ret != 0)
        return NULL;

    for(size_t i = 0; i < g.gl_pathc; i++){
        char path[IMU_MAX_PATH_LEN] = "";
        char buf[64] = "";

        const char* gPath = g.gl_pathv[i];

        snprintf(path, IMU_MAX_PATH_LEN, "%s/name", gPath);

        FILE* f = fopen(path, "r");
        if(f == NULL)
            continue;

        char* d = fgets(buf, sizeof(buf), f);
        fclose(f);

        if(d == NULL)
            continue;

        buf[strcspn(buf, "\n")] = '\0';

        if(strcmp(buf, name) == 0){
            result = strdup(gPath);
            break;
        }
    }

    globfree(&g);
    return result;
}

static int readFloatSysfs(const char* path, float* out){
    FILE* f = fopen(path, "r");
    if(f == NULL)
        return -1;

    int n = fscanf(f, "%f", out);
    fclose(f);

    return (n == 1) ? 0 : -1;
}

static int writeSysfsInt(const char* base, const char* attr, int val){
    char path[IMU_MAX_PATH_LEN];
    FILE* f;

    snprintf(path, sizeof(path), "%s%s", base, attr);

    f = fopen(path, "w");
    if(f == NULL)
        return -1;

    fprintf(f, "%d", val);
    fclose(f);

    return 0;
}

static int imuScalesInit(const char* base, imuScales_t* sc){
    char path[IMU_MAX_PATH_LEN];

    snprintf(path, sizeof(path), "%sin_accel_scale", base);
    if(readFloatSysfs(path, &sc->accel) != 0)
        return -1;

    snprintf(path, sizeof(path), "%sin_magn_scale", base);
    if(readFloatSysfs(path, &sc->magn) != 0)
        return -1;

    snprintf(path, sizeof(path), "%sin_anglvel_scale", base);
    if(readFloatSysfs(path, &sc->anglvel) != 0)
        return -1;

    snprintf(path, sizeof(path), "%sin_rot_scale", base);
    if(readFloatSysfs(path, &sc->rot) != 0)
        return -1;

    return 0;
}

static int imuWriteMetadata(const imuScales_t* sc){
    FILE* f = fopen(IMU_META_PATH, "w");
    if(f == NULL){
        perror("imuWriteMetadata fopen");
        return -1;
    }

    fprintf(f,
        "# BNO055 IMU metadata\n"
        "# Raw record (imuRaw_t): 56 bytes, little-endian, packed.\n"
        "# Field order, all int16 unless noted:\n"
        "#   accel[3] magn[3] anglvel[3] yaw roll pitch quat[4](w,x,y,z)\n"
        "#   accel_linear[3] gravity[3] <4 pad bytes> timestamp(int64)\n"
        "# timestamp: CLOCK_REALTIME ns, UTC. Physical value = raw * scale.\n"
        "# Units: accel/linear/gravity m/s^2, anglvel rad/s, euler degrees,\n"
        "#        quat dimensionless, magn driver units (~Gauss).\n"
        "accel_scale=%.9g\n"
        "magn_scale=%.9g\n"
        "anglvel_scale=%.9g\n"
        "rot_scale=%.9g\n"
        "quat_scale=%.9g\n",
        (double)sc->accel, (double)sc->magn, (double)sc->anglvel,
        (double)sc->rot, (double)IMU_QUAT_SCALE);

    fclose(f);
    return 0;
}

int imuInit(imuThreadArgs_t* a, imuShared_t* shared){
    imuScales_t scales;
    char* sysPath = getIMUPathByName(IMU_DEV_NAME);
    const char* leaf;

    if(sysPath == NULL){
        fprintf(stderr, "imuInit: device '%s' not found\n", IMU_DEV_NAME);
        return -1;
    }

    leaf = strrchr(sysPath, '/');
    if(leaf == NULL){
        free(sysPath);
        return -1;
    }

    snprintf(a->devNode, sizeof(a->devNode), "/dev%s", leaf);
    snprintf(a->sysBase, sizeof(a->sysBase), "%s/", sysPath);
    free(sysPath);

    if(imuScalesInit(a->sysBase, &scales) != 0){
        fprintf(stderr, "imuInit: cannot read scales\n");
        return -1;
    }

    if(imuWriteMetadata(&scales) != 0)
        fprintf(stderr, "imuInit: warning, cannot write %s\n", IMU_META_PATH);

    a->evfdStop = eventfd(0, EFD_NONBLOCK);
    if(a->evfdStop < 0){
        perror("imuInit eventfd");
        return -1;
    }

    a->shared = shared;
    shared->valid = 0;

    return 0;
}

int imuGetSnapshot(imuShared_t* sh, pthread_mutex_t* mtx, imuRaw_t* out){
    int ok;

    pthread_mutex_lock(mtx);
    *out = sh->latest;
    ok   = sh->valid;
    pthread_mutex_unlock(mtx);

    return ok;
}

void* imuThread(void* arg){
    imuThreadArgs_t* a = (imuThreadArgs_t*)arg;
    struct pollfd pfd[2];
    uint8_t raw[IMU_SCAN_SIZE];
    int fd = open(a->devNode, O_RDONLY);

    if(fd < 0){
        perror("imuThread open");
        return NULL;
    }

    pfd[0].fd     = fd;
    pfd[0].events = POLLIN;
    pfd[1].fd     = a->evfdStop;
    pfd[1].events = POLLIN;

    while(1){
        int ret = poll(pfd, 2, -1);

        if(ret < 0){
            if(errno == EINTR)
                continue;
            perror("imuThread poll");
            break;
        }

        if(pfd[1].revents & POLLIN){
            uint64_t drain;
            read(a->evfdStop, &drain, sizeof(drain));
            break;
        }

        if(pfd[0].revents & POLLIN){
            ssize_t n = read(fd, raw, IMU_SCAN_SIZE);
            if(n == IMU_SCAN_SIZE){
                pthread_mutex_lock(a->mtx);
                memcpy(&a->shared->latest, raw, IMU_SCAN_SIZE);
                a->shared->valid = 1;
                pthread_mutex_unlock(a->mtx);
            }else if(n < 0 && errno != EAGAIN){
                perror("imuThread read");
                break;
            }
        }
    }

    close(fd);
    return NULL;
}

int imuStart(imuThreadArgs_t* a, pthread_t* tid){
    uint8_t probe[IMU_SCAN_SIZE];
    ssize_t n;
    int fd;

    if(writeSysfsInt(a->sysBase, "buffer0/enable", 1) != 0){
        fprintf(stderr, "imuStart: cannot enable buffer\n");
        return -1;
    }

    fd = open(a->devNode, O_RDONLY);
    if(fd < 0){
        perror("imuStart open");
        writeSysfsInt(a->sysBase, "buffer0/enable", 0);
        return -1;
    }

    n = read(fd, probe, sizeof(probe));
    close(fd);

    if(n != IMU_SCAN_SIZE){
        fprintf(stderr, "imuStart: unexpected scan_size (%zd != %d), abort\n",
                n, IMU_SCAN_SIZE);
        writeSysfsInt(a->sysBase, "buffer0/enable", 0);
        return -1;
    }

    return pthread_create(tid, NULL, &imuThread, a);
}

void imuStop(imuThreadArgs_t* a, pthread_t tid){
    uint64_t one = 1;

    write(a->evfdStop, &one, sizeof(one));
    pthread_join(tid, NULL);
    writeSysfsInt(a->sysBase, "buffer0/enable", 0);
}
