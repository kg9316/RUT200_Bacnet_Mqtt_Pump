#include "gateway.h"

#include <ctype.h>
#include <stdio.h>
#include <time.h>

uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

time_t unix_time_now(void)
{
    return time(NULL);
}

void safe_copy(char *dst, size_t n, const char *src)
{
    if (!dst || !n)
        return;

    snprintf(dst, n, "%s", src ? src : "");
}

void topic_sanitize(char *s)
{
    size_t i;

    if (!s)
        return;

    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.'))
            s[i] = '_';
    }
}

bool double_changed(double a, double b)
{
    double d = a - b;

    if (d < 0.0)
        d = -d;

    return d > 0.000001;
}
