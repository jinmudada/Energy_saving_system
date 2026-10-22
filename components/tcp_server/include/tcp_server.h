#pragma once
#include <stdbool.h>

void tcp_server_init(void);
void tcp_server_accept_task(void *pvParameters);
bool tcp_server_send_message(const char *message);
