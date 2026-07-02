#ifndef IMU_H_
#define IMU_H_

#include "main.h"
#include <dirent.h>
#include <glob.h>
#include <endian.h>

#define IMU_DEV_NAME     "bno055"
#define IMU_MAX_PATH_LEN 1024

#define IMU_QUAT_SCALE   (1.0f / 16384.0f)

#define IMU_META_PATH    "/srv/ftp/imu_metadata.txt"

int   imuInit(imuThreadArgs_t* a, imuShared_t* shared);
int   imuGetSnapshot(imuShared_t* sh, pthread_mutex_t* mtx, imuRaw_t* out);
void* imuThread(void* arg);
int   imuStart(imuThreadArgs_t* a, pthread_t* tid);
void  imuStop(imuThreadArgs_t* a, pthread_t tid);

#endif
