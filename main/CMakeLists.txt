idf_component_register(SRCS "Energy_saving_system.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_manager time_sync tcp_server dht11 light_sensor esp_http_client nvs_flash mbedtls)
