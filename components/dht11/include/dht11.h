#pragma once
#include <stdbool.h>

void dht11_init(void);
bool dht11_read(float *temperature, float *humidity);
