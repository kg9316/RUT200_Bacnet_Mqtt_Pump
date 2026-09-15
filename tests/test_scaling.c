/* Deterministic host-only capacity/lookup regression, no router or memory test. */
#include <assert.h>
#include <stdio.h>
#include "../src/device_table.c"
uint64_t monotonic_ms(void) { return 100; }
int main(void)
{
    for (unsigned d=0; d<100; ++d) {
        DEVICE_STATE *device=get_or_create_device(d);
        for (unsigned p=0; p<100; ++p) {
            POINT_STATE *point=add_point(device,OBJECT_ANALOG_INPUT,p);
            assert(point); point->numeric_value=d*100+p;
        }
    }
    assert(total_points==10000);
    assert(!add_point(find_device(0),OBJECT_ANALOG_INPUT,100));
    for (unsigned d=0; d<100; ++d)
        for (unsigned p=0; p<100; ++p) {
            POINT_STATE *point=add_point(find_device(d),OBJECT_ANALOG_INPUT,p);
            assert(point && point->numeric_value==d*100+p);
        }
    device_table_cleanup();
    DEVICE_STATE *device=get_or_create_device(0);
    assert(add_point(device,OBJECT_ANALOG_INPUT,0));
    assert(total_points==1);
    device_table_cleanup();
    puts("PASS: 100 devices, 10000 points, global limit, lookup identity and cleanup");
}
