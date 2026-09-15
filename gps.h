#ifndef GPS_H_
#define GPS_H_
#include "main.h"
#include "commands.h"

#define GPS_CONF_LEN 8

/* Configuration bytes sent to every GPS UART on "gps configure": the default
 * is the historical fixed string, the command replaces it with its two words */
void gpsSetConf(const uint8_t conf[GPS_CONF_LEN]);

void* gpsCfgIrqThread(void* arg);

void* gpsCtrlThread(void* arg);

#endif
