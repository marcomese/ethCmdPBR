#ifndef GPS_H_
#define GPS_H_
#include "main.h"
#include "commands.h"

void* gpsCfgIrqThread(void* arg);

void* gpsCtrlThread(void* arg);

#endif
