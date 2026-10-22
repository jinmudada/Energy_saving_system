#pragma once
#include <stdbool.h>

void light_sensor_init(void);
int light_sensor_read_raw(void);
bool light_sensor_is_bright(void);
