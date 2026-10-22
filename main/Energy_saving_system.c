#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "wifi_manager.h"
#include "time_sync.h"
#include "tcp_server.h"
#include "dht11.h"
#include "light_sensor.h"
#include "esp_crt_bundle.h"

static const char *TAG = "MAIN_APP";

// 防抖标志位
static volatile bool light_alarm_sent = false;
static volatile bool ac_alarm_sent = false;
static float local_forecast_temp = 25.0f; // 默认天气
static volatile char http_response[1024]; // 用于存放天气API返回的数据
static volatile bool weather_ready = false;// 【新增】天气是否已就绪

/*typedef struct {
    esp_http_client_event_id_t event_id; // 事件类型 ID
    esp_http_client_handle_t client;     // 当前 HTTP 客户端句柄
    void *data;                          // 本次事件携带的数据（比如网络碎片）
    int data_len;                        // 本次数据的长度
    void *user_data;                     // 用户自定义数据（在 config 里传的）
    char *header_key;                    // 响应头的 Key（收到头事件时有效）
    char *header_value;                  // 响应头的 Value
} esp_http_client_event_t;*/
// HTTP 客户端事件回调，接收数据

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->data_len + strlen(http_response) < sizeof(http_response)) {
            strncat(http_response, (char *)evt->data, evt->data_len);
        }
    }
    return ESP_OK;
}

// 获取网络天气 (使用中国天气网免费接口)
static void fetch_weather_task(void *pvParameters) {

    // 等待 WiFi 连接成功
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char url[128];
    // 通过 HTTP Client 请求 Open-Meteo API 获取当地实时天气”这一功能的发令枪。
    //安全地拼接出一个完整的 API 请求 URL 字符串，存入 url 这个字符数组中。
    // 这里使用了 snprintf 函数，它会根据指定的格式把字符串写入 url 数组中，同时确保不会越界。
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=23.11&longitude=114.42&current=temperature_2m");
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 15000,                        // 超时设置，防死等
        .crt_bundle_attach = esp_crt_bundle_attach, // 必须加上，用于HTTPS，挂载 HTTPS 证书
    };
    
    while(1) {
        //将一段内存区域，全部填充为指定的字节值（通常用来清零）
        memset(http_response, 0, sizeof(http_response));
        //创建 HTTP 客户端实例（句柄）
        esp_http_client_handle_t client = esp_http_client_init(&config);
        //执行 HTTP 请求，获取天气数据，访问互联网：esp_http_client_perform ，发起网络通信的核心阻塞函数，依次完成 DNS 域名解析、TCP 三次握手、TLS 加密握手，然后发送 GET 请求
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            // 打印原始数据，方便调试
            ESP_LOGI(TAG, "天气原始数据: %s", http_response);
            
            // 解析 Open-Meteo 的 JSON 数据
            //把刚才拼接好的 JSON 字符串，解析成 cJSON 的树状结构（在堆内存中分配）。
            cJSON *root = cJSON_Parse(http_response);
            if (root) {
                // 【修改1】获取 "current" 对象 (而不是 weatherinfo)
                //获取根节点下的 current 对象
                cJSON *current = cJSON_GetObjectItem(root, "current");
                if (current) {
                    // 【修改2】获取 "temperature_2m" 字段 (而不是 temp)
                    //在 current 节点下，找 temperature_2m 这个字段
                    cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
                    if (temp) {
                            local_forecast_temp = temp->valuedouble; 
                            weather_ready = true; // 标记天气数据已就绪
                            ESP_LOGI(TAG, "更新网络天气(Open-Meteo): 惠州实时温度为 %.1f ℃", local_forecast_temp);
                    }
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGE(TAG, "JSON 解析失败");
            }
        } else {
            //esp_http_client_perform：执行 HTTP 请求，返回错误码。ESP_OK 表示成功，其他值表示失败，并快速定位查找哪里出错。
            ESP_LOGE(TAG, "天气请求失败: %s", esp_err_to_name(err));
        }
        
        //：释放资源
        esp_http_client_cleanup(client);
        
        if (weather_ready) {
            // 如果已经成功获取过天气，等 1 小时再更新
            ESP_LOGI(TAG, "天气获取成功，1小时后再更新");
            vTaskDelay(pdMS_TO_TICKS(3600000)); 
        } else {
            // 如果失败了，等 10 秒立刻重试
            ESP_LOGW(TAG, "天气获取失败，10秒后重试...");
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }
}
 
static int dht11_fail_count = 0; // DHT11连续失败计数器
// 核心监控任务
static void monitor_task(void *pvParameters) {
    // 等待 WiFi 连上
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 启动时间同步
    time_sync_init_sntp();

    struct tm timeinfo;

    while (1) { // <--- 开始主循环
        time_sync_get_local_time(&timeinfo);
        ESP_LOGI(TAG, "当前时间: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        float temperature = 0.0f; 
        float humidity = 0.0f;

        // ==========================================
        // 规则1: 22点后查灯光
        // ==========================================
        // 保留调试日志：打印光敏当前的 ADC 原始值
        int raw_light = light_sensor_read_raw();
        ESP_LOGI(TAG, "当前光敏 ADC 值: %d", raw_light);

        // 时间限制：只有 22点以后 或 早上6点前 才报警
        if (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 6) { 
            if (light_sensor_is_bright()) {
                if (!light_alarm_sent) {
                    if (tcp_server_send_message("【节能警报】22点后光照异常，请确认灯光是否未关！\n")) {
                        light_alarm_sent = true; 
                    }
                }
            } else {
                light_alarm_sent = false; // 灯关了，重置状态
            }
        } else {
            light_alarm_sent = false; // 不在时间段，重置状态
        }

        // ==========================================
        // 规则2: 温差查空调
        // ==========================================
        if (weather_ready) { 
            // 1. 尝试读取 DHT11
            if (dht11_read(&temperature, &humidity)) { 
                dht11_fail_count = 0; 
                (void)humidity; 
                
                float diff = temperature - local_forecast_temp;
                
                // 【保留日志】无论白天还是晚上，都打印温度，方便调试
                ESP_LOGI(TAG, "室内温度: %.1f℃, 预报温度: %.1f℃, 温差: %.1f℃", 
                         temperature, local_forecast_temp, diff);

                // 【时间限制】只有 22点以后 或 早上6点前，才判断是否报警
                if (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 6) { 
                    
                    // 2. 判断温差是否超标
                    if (diff > 4.0f || diff < -4.0f) { 
                        ESP_LOGW(TAG, "温差超限！当前防抖状态 ac_alarm_sent = %d", ac_alarm_sent);                    
                        
                        if (!ac_alarm_sent) { 
                            char msg[128];
                            //装货snprintf函数：把格式化的字符串写入 msg 数组中，确保不会越界。
                            snprintf(msg, sizeof(msg), 
                                     "【节能警报】室内(%.1f℃)与预报(%.1f℃)相差超4度，请检查空调！\n",
                                     temperature, local_forecast_temp);

                            // 3. 只发送一次，并记录结果
                            //发送send_message函数返回true表示发送成功，false表示失败（可能没有客户端连接）
                            if (tcp_server_send_message(msg)) {
                                ac_alarm_sent = true;
                                ESP_LOGI(TAG, "TCP 发送成功，已锁定防抖状态");
                            } else {
                                ESP_LOGE(TAG, "TCP 发送失败(可能没客户端连接)，下次再试");
                            }                             
                        } 
                    } else {
                        ac_alarm_sent = false; // 温差恢复正常，重置状态
                    } 
                    
                } else {
                    // 【重置】不在晚上时间段，强制重置防抖状态，为晚上报警做准备
                    ac_alarm_sent = false;
                }
                
            } else {
                // DHT11 读取失败的处理
                dht11_fail_count++;
                if (dht11_fail_count >= 3) {
                    ESP_LOGE(TAG, "DHT11 连续读取失败！请检查接线。");
                } else {
                    ESP_LOGW(TAG, "DHT11 偶发读取失败(第%d次)，等待重试...", dht11_fail_count);
                }
            }
        } else {
            // 天气还没准备好的处理
            // ESP_LOGW(TAG, "天气数据未就绪，跳过温差检测");
        }

        // 每 5 秒循环一次
        vTaskDelay(pdMS_TO_TICKS(5000));
    } // <--- 闭合 while(1) 主循环
} // <--- 闭合 monitor_task 函数

void app_main(void) {
    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化传感器
    light_sensor_init();
    dht11_init();

    // 3. 启动 WiFi (参数来自 menuconfig)
    wifi_manager_init_sta(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

    // 4. 启动 TCP 服务器
    tcp_server_init();
    xTaskCreate(tcp_server_accept_task, "tcp_accept", 4096, NULL, 5, NULL);

    // 5. 开启天气获取任务 和 核心监控任务
    xTaskCreate(fetch_weather_task, "fetch_weather", 4096, NULL, 4, NULL);
    xTaskCreate(monitor_task, "monitor_task", 4096, NULL, 5, NULL);
}

