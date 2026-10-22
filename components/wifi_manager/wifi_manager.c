#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";
static volatile bool s_wifi_connected = false;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        ESP_LOGI(TAG, "WiFi断开，正在重连...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "成功获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
    }
}

void wifi_manager_init_sta(const char *ssid, const char *password) {
    //初始化底层 TCP/IP 协议栈（LwIP）
    ESP_ERROR_CHECK(esp_netif_init());
    //创建默认的事件循环，这是 ESP-IDF 异步通信的核心
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //创建默认的 Wi-Fi 网络接口对象，将 Wi-Fi 驱动与 IP 协议栈绑定
    esp_netif_create_default_wifi_sta();

    //使用默认的 Wi-Fi 驱动配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    //注册 Wi-Fi 和 IP 事件的回调函数
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    //配置 Wi-Fi 连接参数
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    //设置 Wi-Fi 模式为 STA（Station，客户端模式）
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    //下发配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    //启动 Wi-Fi 驱动
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "正在连接 WiFi: %s ...", ssid);
}

//查询 Wi-Fi 是否已连接
bool wifi_manager_is_connected(void) {
    return s_wifi_connected;
}
