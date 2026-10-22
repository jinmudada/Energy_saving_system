#pragma once
#include <time.h>

void time_sync_init_sntp(void);
void time_sync_get_local_time(struct tm *timeinfo);
