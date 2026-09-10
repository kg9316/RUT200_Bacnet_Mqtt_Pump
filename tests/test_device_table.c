#include <assert.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <string.h>
#include "../src/device_table.c"
uint64_t monotonic_ms(void) { return 100; }
int main(void) {
    DEVICE_STATE *d = get_or_create_device(1);
    assert(d && !d->points && !d->point_capacity);
    POINT_STATE *p=add_point(d,OBJECT_ANALOG_INPUT,0);
    strcpy(p->name,"retained");
    g_request.device=d; g_request.point=p;
    for(unsigned i=1;i<10000;i++) assert(add_point(d,OBJECT_ANALOG_INPUT,i));
    assert(d->point_count==10000 && d->point_capacity==10000);
    assert(g_request.point==&d->points[0]);
    assert(strcmp(g_request.point->name,"retained")==0);
    assert(!add_point(d,OBJECT_ANALOG_INPUT,10000));
    assert(add_point(d,OBJECT_ANALOG_INPUT,0)==&d->points[0]);
    for(unsigned i=2;i<=500;i++) assert(get_or_create_device(i));
    assert(device_count()==500 && !get_or_create_device(501));
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    printf("MEMORY: point=%lu bytes, 10000-point capacity=%lu bytes, device slots=%lu bytes, host peak RSS=%ld KiB\n", (unsigned long)sizeof(POINT_STATE), (unsigned long)(d->point_capacity*sizeof(POINT_STATE)), (unsigned long)sizeof(g_devices), usage.ru_maxrss);
    device_table_cleanup();
    assert(!device_count() && !g_request.point);
    puts("PASS: 10000 points, 500 devices, dynamic growth, retained data and request pointers, cleanup");
}
