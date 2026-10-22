#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <sys/time.h>
#include "esp_netif_sntp.h"

static const char *TAG = "TIME_SYNC";

void time_sync_init_sntp(void) {
    ESP_LOGI(TAG, "初始化 SNTP...");
    //接入 SNTP 网络时间同步
    //配置 SNTP 客户端参数。
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    //初始化 SNTP 模块（传入了你刚配置好的结构体），仅是发起了同步请求，它立刻就会返回
    esp_netif_sntp_init(&config);

    // 设置为北京时间 (UTC+8)
    //设置环境变量
    setenv("TZ", "CST-8", 1);
    //通知底层时间库
    tzset();
    //SNTP 时间同步模块的收尾动作，等待同步完成，最多等 10 秒
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "时间同步超时");
    } else {
        ESP_LOGI(TAG, "时间同步成功");
    }
}

//整个时间模块最终对外输出的“统一接口”。
void time_sync_get_local_time(struct tm *timeinfo) {
    //获取当前的时间戳
    time_t now;
    time(&now);
    //将时间戳转换为本地时间（struct tm 结构体）
    localtime_r(&now, timeinfo);
}
