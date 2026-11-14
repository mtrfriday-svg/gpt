#pragma once
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <string>

class StatusDisplay {
public:
    StatusDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel);
    static StatusDisplay* GetInstance();
    static void SetInstance(StatusDisplay* instance);
    void Start();
    void SetState(const char* state);
    void SetChat(const char* role, const char* text);
    void Sleep();
private:
    static void Task(void* arg);
    void Render();
    void Text(int x, int y, const std::string& value, int scale, uint16_t color);
    void Pixel(int x, int y, uint16_t color);
    void Flush();
    esp_lcd_panel_io_handle_t io_;
    esp_lcd_panel_handle_t panel_;
    uint16_t* framebuffer_ = nullptr;
    uint16_t* dma_a_ = nullptr;
    uint16_t* dma_b_ = nullptr;
    std::mutex mutex_;
    std::string state_ = "Starting";
    std::string chat_ = "";
    bool running_ = false;
};
