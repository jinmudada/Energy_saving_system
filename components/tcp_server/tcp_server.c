#include "tcp_server.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // 【新增】引入信号量头文件，用于多任务互斥
#include <string.h>
#include <unistd.h>

static const char *TAG = "TCP_SERVER";
#define TCP_PORT 8080

static int server_sock = -1;
static int client_sock = -1;

// 【新增】定义互斥锁句柄，防止多个任务同时调用 send 导致数据粘包/乱码
static SemaphoreHandle_t xTcpMutex = NULL; 

void tcp_server_init(void) {
    /* 
     * struct sockaddr_in 详解：
     * 这是 IPv4 的地址结构体，用来告诉 Socket 我们的服务器要绑定在哪个 IP 和端口上。
     * 你的注释非常准确！
     */
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY 表示监听本机所有网卡（WiFi、以太网等）
    dest_addr.sin_family = AF_INET;                // 协议族：IPv4
    dest_addr.sin_port = htons(TCP_PORT);          // 端口号：必须用 htons 转为网络字节序（大端）

    // 【新增】在使用锁之前，必须先创建互斥锁
    xTcpMutex = xSemaphoreCreateMutex();
    if (xTcpMutex == NULL) {
        ESP_LOGE(TAG, "创建 TCP 互斥锁失败！内存不足");
        return;
    }

    // 1. 创建一个 Socket（套接字）。
    // AF_INET: IPv4, SOCK_STREAM: TCP 协议, IPPROTO_IP: 默认协议
    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    
    // 2. 绑定（Bind）IP和端口。成功返回 0，失败返回 -1（非 0）。
    int err = bind(server_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(server_sock); // 资源回收
        return;
    }
    
    // 3. 监听（Listen）。参数 1 表示允许 1 个客户端排队等待连接
    err = listen(server_sock, 1);
    ESP_LOGI(TAG, "TCP 服务器已启动，监听端口 %d", TCP_PORT);
}

void tcp_server_accept_task(void *pvParameters) {
    while (1) {
        // 用来保存连进来的手机（客户端）的 IP 和端口信息
        struct sockaddr_in source_addr; 
        socklen_t addr_len = sizeof(source_addr);

        // 4. 阻塞等待客户端连接。
        // 如果没有手机连接，程序会一直停在这里（不占用 CPU）
        int new_sock = accept(server_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        if (new_sock >= 0) {
            // 如果之前有旧连接，先安全地关掉它（保证系统只服务一个客户端）
            if (client_sock >= 0) {
                close(client_sock);
            }
            
            // 获取互斥锁，安全地更新全局变量 client_sock
            if (xSemaphoreTake(xTcpMutex, portMAX_DELAY) == pdTRUE) {
                client_sock = new_sock;
                ESP_LOGI(TAG, "客户端已连接，分配的 Socket ID: %d", client_sock);
                xSemaphoreGive(xTcpMutex); // 释放锁
            }
            
            // 发送欢迎语
            tcp_server_send_message("欢迎连接节能系统！\n"); 
        }
    }
}

bool tcp_server_send_message(const char *message) {
    if (message == NULL) return false;

    // 1. 获取互斥锁。
    // 如果有两个任务同时要发消息，后来的任务会在这里等最多 1 秒
    if (xSemaphoreTake(xTcpMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取发送锁超时，可能有其他任务正在发送");
        return false;
    }

    // 检查是否有客户端连接
    if (client_sock < 0) {
        ESP_LOGW(TAG, "无客户端连接，消息未发送");
        xSemaphoreGive(xTcpMutex); // 记得提前释放锁
        return false;
    }
    //sizeof 是运算符（算内存大小），strlen 是函数（算字符串有效长度）
    size_t total_len = strlen(message);
    size_t sent_len = 0;
    bool success = true;

    // 2. 循环发送，处理 TCP 的“短写”问题。
    // send 函数不一定能一次性把所有数据发完，所以用 while 循环确保发全。
    while (sent_len < total_len) {
        // 注意：每次从 message + sent_len 的偏移位置开始发，发送剩余长度
        //send(发给谁, 指针偏移, 发多少, 发送标志位 Flags)
        int ret = send(client_sock, message + sent_len, total_len - sent_len, 0);
        
        if (ret < 0) {
            ESP_LOGE(TAG, "发送失败: errno %d", errno);
            success = false;
            break;
        } else if (ret == 0) {
            // 返回 0 说明对端（手机）主动断开了连接
            ESP_LOGW(TAG, "客户端已断开连接");
            success = false;
            break;
        }
        
        sent_len += ret; // 累加实际发送成功的字节数
    }

    // 3. 发送失败，清理并重置 Socket，防止僵尸连接
    if (!success) {
        close(client_sock);
        client_sock = -1;
    }

    // 4. 无论发送成功还是失败，最后都必须释放互斥锁！
    xSemaphoreGive(xTcpMutex);
    return success;
}
