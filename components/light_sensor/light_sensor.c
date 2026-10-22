#include "light_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 建议用宏定义采样次数，方便以后修改
#define SAMPLE_COUNT 10 

static const char *TAG = "LIGHT_SENSOR";
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_3 // 对应 GPIO3
//申明ADC单次转换句柄：adc_oneshot_unit_handle_t | 连续转换（DMA）:adc_continuous_unit_handle_t
static adc_oneshot_unit_handle_t adc_handle;

void light_sensor_init(void) {
    //申明ADC单次转换配置结构体：adc_oneshot_unit_init_cfg_t | 连续转换（DMA）:adc_continuous_init_cfg_t。（整机配置）
    //初始化ADC单次转换配置结构体并选择ADC1
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    //向系统申请硬件资源：adc_oneshot_new_unit（入参，出参）
    //ESP_ERROR_CHECK：检查函数返回值是否为ESP_OK（值为0），如果不是则打印错误信息并终止程序
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));
    //具体配置ADC通道的采样参数：adc_oneshot_chan_cfg_t | 连续转换（DMA）:adc_continuous_chan_cfg_t
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    //绑定ADC通道和配置参数：adc_oneshot_config_channel（入参（adc_handle），入参（GPIO3），入参（config））
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LIGHT_ADC_CHANNEL, &config));
    ESP_LOGI(TAG, "光敏初始化完成 (GPIO3)");
}

//平均滤波
//int light_sensor_read_raw(void) {
//    int raw_value = 0;
//    int sum = 0;
//    
//   // 连续采样 10 次，求平均值，过滤掉瞬间的干扰
//    for (int i = 0; i < 10; i++) {
//        //读取ADC通道的原始值：adc_oneshot_read（入参（adc_handle），入参（LIGHT_ADC_CHANNEL），出参（raw_value）），成功返回ESP_OK
//        adc_oneshot_read(adc_handle, LIGHT_ADC_CHANNEL, &raw_value);
//        sum += raw_value;
//    
//       // 每次采样间隔 2 毫秒
//        vTaskDelay(pdMS_TO_TICKS(2)); 
//    }
//    
//    return sum / 10; 
//}

//去极值滤波
int light_sensor_read_raw(void) {
    int raw_values[SAMPLE_COUNT]; 
    int sum = 0;
    int max_val = 0;             
    int min_val = 4095;          
    
    // 1. 连续采样 10 次
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int temp_val = 0;
        if (adc_oneshot_read(adc_handle, LIGHT_ADC_CHANNEL, &temp_val) != ESP_OK) {
            return -1; // 读取失败
        }
        raw_values[i] = temp_val;
        
        // 建议保留这个延时，避免连续采样过快导致读数粘连
        vTaskDelay(pdMS_TO_TICKS(2)); 
    }
    
    // 2. 找极值并求和
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum += raw_values[i];
        if (raw_values[i] > max_val) max_val = raw_values[i]; 
        if (raw_values[i] < min_val) min_val = raw_values[i]; 
    }
    
    // 3. 去掉最高和最低，求平均
    return (sum - max_val - min_val) / (SAMPLE_COUNT - 2);
}

bool light_sensor_is_bright(void) {
    int raw = light_sensor_read_raw();
    // 如果读取失败(-1)，直接返回上一次的状态，或者默认认为“暗”（不报警）
    if (raw < 0) {
        return false; 
    }
    //模块是反相的（光越强值越小），所以用 <
    return raw < CONFIG_LIGHT_THRESHOLD;
}
