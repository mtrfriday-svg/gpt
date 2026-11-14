#include "status_display.h"
#include "config.h"
#include "mdee_robot.h"
#include "font6x8.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {
StatusDisplay* s_status_display = nullptr;
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kRed = 0xF800;
constexpr int kStripRows = 20;
}

StatusDisplay* StatusDisplay::GetInstance() { return s_status_display; }
void StatusDisplay::SetInstance(StatusDisplay* instance) { s_status_display = instance; }

StatusDisplay::StatusDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
    : io_(io), panel_(panel) {
    framebuffer_ = static_cast<uint16_t*>(heap_caps_malloc(STATUS_WIDTH * STATUS_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    dma_a_ = static_cast<uint16_t*>(heap_caps_malloc(STATUS_WIDTH * kStripRows * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    dma_b_ = static_cast<uint16_t*>(heap_caps_malloc(STATUS_WIDTH * kStripRows * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

void StatusDisplay::Start() {
    if (running_ || framebuffer_ == nullptr || dma_a_ == nullptr || dma_b_ == nullptr) return;
    running_ = true;
    xTaskCreate(Task, "mdee_status", 4096, this, 2, nullptr);
}
void StatusDisplay::SetState(const char* state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state == nullptr ? "" : state;
}
void StatusDisplay::SetChat(const char* role, const char* text) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string value = role == nullptr ? "" : role;
    value += ": ";
    value += text == nullptr ? "" : text;
    chat_ = value.substr(0, 58);
}
void StatusDisplay::Sleep() {
    if (panel_ != nullptr) esp_lcd_panel_disp_on_off(panel_, false);
}
void StatusDisplay::Pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= STATUS_WIDTH || y < 0 || y >= STATUS_HEIGHT) return;
    framebuffer_[y * STATUS_WIDTH + x] = color;
}
void StatusDisplay::Text(int x, int y, const std::string& value, int scale, uint16_t color) {
    int cursor = x;
    for (unsigned char ch : value) {
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t* glyph = kMdeeFont6x8[ch - 32];
        for (int col = 0; col < 6; ++col) {
            for (int row = 0; row < 8; ++row) {
                if ((glyph[col] & (1U << row)) == 0) continue;
                for (int sx = 0; sx < scale; ++sx)
                    for (int sy = 0; sy < scale; ++sy)
                        Pixel(cursor + col * scale + sx, y + row * scale + sy, color);
            }
        }
        cursor += 7 * scale;
        if (cursor > STATUS_WIDTH - 6 * scale) break;
    }
}
void StatusDisplay::Render() {
    if (framebuffer_ == nullptr) return;
    std::fill(framebuffer_, framebuffer_ + STATUS_WIDTH * STATUS_HEIGHT, kBlack);
    std::string state, chat;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = state_;
        chat = chat_;
    }
    auto& robot = MdeeRobot::GetInstance();
    Text(12, 10, "M'DEE", 3, kCyan);
    Text(166, 20, "WIFI", 1, kWhite);
    Text(12, 54, state.substr(0, 26), 2, kWhite);

    char battery[32];
    const float volts = robot.BatteryVolts();
    const int pct = robot.BatteryPercent();
    snprintf(battery, sizeof(battery), "%d%% %.2fV", pct, volts);
    const uint16_t color = pct > 40 ? kGreen : (pct > 15 ? kYellow : kRed);
    Text(12, 88, battery, 3, color);
    const int width = 210 * pct / 100;
    for (int y = 132; y < 144; ++y)
        for (int x = 12; x < 222; ++x)
            Pixel(x, y, x < 12 + width ? color : 0x4208);

    char distances[40];
    snprintf(distances, sizeof(distances), "F:%dcm  R:%dcm",
        robot.FrontCm(), robot.RearCm());
    Text(12, 158, distances, 2, kWhite);
    Text(12, 190, MdeeRobot::ModeName(robot.GetMode()), 2, kCyan);
    Text(12, 218, chat.substr(0, 30), 1, kWhite);
    Flush();
}
void StatusDisplay::Flush() {
    uint16_t* buffers[2] = {dma_a_, dma_b_};
    for (int y = 0, n = 0; y < STATUS_HEIGHT; y += kStripRows, ++n) {
        uint16_t* dst = buffers[n & 1];
        const int rows = std::min(kStripRows, STATUS_HEIGHT - y);
        memcpy(dst, framebuffer_ + y * STATUS_WIDTH, STATUS_WIDTH * rows * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(panel_, 0, y, STATUS_WIDTH, y + rows, dst);
    }
}
void StatusDisplay::Task(void* arg) {
    auto* self = static_cast<StatusDisplay*>(arg);
    while (true) {
        self->Render();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
