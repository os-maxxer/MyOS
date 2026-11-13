#ifndef SOLIS_DOOM_SYS_TIME_H
#define SOLIS_DOOM_SYS_TIME_H

typedef long time_t;

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *out, void *timezone);

#endif
