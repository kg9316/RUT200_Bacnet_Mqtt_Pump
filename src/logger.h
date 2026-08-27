#ifndef GK_LOGGER_H
#define GK_LOGGER_H

#include <syslog.h>

#define LOG_OPEN() openlog("gk-bacnet-mqtt", LOG_PID | LOG_NDELAY, LOG_DAEMON)
#define LOG_CLOSE() closelog()
#define LOG_DEBUGF(...) syslog(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFOF(...)  syslog(LOG_INFO, __VA_ARGS__)
#define LOG_WARNF(...)  syslog(LOG_WARNING, __VA_ARGS__)
#define LOG_ERRORF(...) syslog(LOG_ERR, __VA_ARGS__)

#endif
