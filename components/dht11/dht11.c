#include "dht11.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT11";
#define DHT11_GPIO GPIO_NUM_4

void dht11_init(void) {
    gpio_reset_pin(DHT11_GPIO);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT11_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "DHT11 初始化完成 (GPIO4)");
}

// 读取一帧 40 位数据
bool dht11_read(float *temperature, float *humidity) {
    uint8_t data[5] = {0};
    
    // 1. 发送起始信号 (拉低 > 18ms)
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 0);
    //pdMS_TO_TICKS：转换为系统节拍tick，
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // 2. 拉高 30us 等待 DHT11 响应
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(30);
    
    // 3. 切换为输入模式接收数据
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    // 等待 DHT11 响应低电平 (约 80us)
    uint32_t timeout = 0;
    while (gpio_get_level(DHT11_GPIO) == 1) {
        if (++timeout > 1000) return false;
        esp_rom_delay_us(1);
    }
    // 等待 DHT11 响应高电平 (约 80us)
    timeout = 0;
    while (gpio_get_level(DHT11_GPIO) == 0) {
        if (++timeout > 1000) return false;
        esp_rom_delay_us(1);
    }
    timeout = 0;
    while (gpio_get_level(DHT11_GPIO) == 1) {
        if (++timeout > 1000) return false;
        esp_rom_delay_us(1);
    }

    // 4. 读取 40 位数据
    for (int i = 0; i < 40; i++) {
        // 等待 50us 低电平结束
        timeout = 0;
        while (gpio_get_level(DHT11_GPIO) == 0) {
            if (++timeout > 1000) return false;
            esp_rom_delay_us(1);
        }
        
        // 高电平时长决定是 0 还是 1 (26-28us 为 0, 70us 为 1)
        esp_rom_delay_us(40); 
        data[i / 8] <<= 1;
        if (gpio_get_level(DHT11_GPIO) == 1) {
            data[i / 8] |= 1;
            timeout = 0;
            // 等待高电平结束
            while (gpio_get_level(DHT11_GPIO) == 1) {
                if (++timeout > 1000) return false;
                esp_rom_delay_us(1);
            }
        }
    }

    // 5. 校验和验证 (前 4 字节之和 == 第 5 字节)
    if ((data[0] + data[1] + data[2] + data[3]) != data[4]) {
        ESP_LOGW(TAG, "DHT11 校验和错误");
        return false;
    }

    *humidity = (float)data[0] + (float)data[1] * 0.1f;
    *temperature = (float)data[2] + (float)data[3] * 0.1f;
    return true;
}
