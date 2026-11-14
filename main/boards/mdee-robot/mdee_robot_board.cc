#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "backlight.h"
#include "config.h"
#include "mcp_server.h"
#include "led/led.h"
#include "face_display.h"
#include "mdee_robot.h"
#include "status_display.h"
#include "web_control.h"

#include <esp_log.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_netif.h>
#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

#define TAG "MdeeBoard"

class MdeeBoard : public WifiBoard {
private:
    Button boot_button_;
    PwmBacklight backlight_;
    FaceDisplay* face_display_ = nullptr;
    StatusDisplay* status_display_ = nullptr;

    esp_lcd_panel_io_handle_t CreatePanelIo(gpio_num_t cs_pin) {
        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_spi_config_t cfg = {};
        cfg.cs_gpio_num = cs_pin;
        cfg.dc_gpio_num = DISPLAY_DC_PIN;
        cfg.spi_mode = DISPLAY_SPI_MODE;
        cfg.pclk_hz = DISPLAY_PCLK_HZ;
        cfg.trans_queue_depth = 8;
        cfg.lcd_cmd_bits = 8;
        cfg.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &cfg, &io));
        return io;
    }

    esp_lcd_panel_handle_t CreatePanel(esp_lcd_panel_io_handle_t io, gpio_num_t reset_pin) {
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t cfg = {};
        cfg.reset_gpio_num = reset_pin;
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &cfg, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        return panel;
    }

    void InitializeDisplays() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = DISPLAY_MOSI_PIN;
        bus.miso_io_num = GPIO_NUM_NC;
        bus.sclk_io_num = DISPLAY_CLK_PIN;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = DISPLAY_MAX_TRANSFER;
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

        auto face_io = CreatePanelIo(FACE_CS_PIN);
        auto face_panel = CreatePanel(face_io, DISPLAY_RST_PIN);
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(face_panel, FACE_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(face_panel, FACE_MIRROR_X, FACE_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(face_panel, FACE_OFFSET_X, FACE_OFFSET_Y));
        face_display_ = new FaceDisplay(face_io, face_panel, FACE_WIDTH, FACE_HEIGHT,
            FACE_OFFSET_X, FACE_OFFSET_Y, FACE_MIRROR_X, FACE_MIRROR_Y, FACE_SWAP_XY);

        // The status panel shares SPI, DC and clock but has its own CS. Its reset is software-only.
        auto status_io = CreatePanelIo(STATUS_CS_PIN);
        auto status_panel = CreatePanel(status_io, GPIO_NUM_NC);
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(status_panel, false));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(status_panel, false, false));
        status_display_ = new StatusDisplay(status_io, status_panel);
        StatusDisplay::SetInstance(status_display_);
        status_display_->Start();
    }

    void InitializeButtons() {
        boot_button_.OnClick([]() {
            Application::GetInstance().ToggleChatState();
        });
        boot_button_.OnDoubleClick([]() {
            MdeeRobot::GetInstance().Stop();
        });
        boot_button_.OnLongPress([]() {
            MdeeRobot::GetInstance().Power(true);
        });
    }

    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();
        auto& robot = MdeeRobot::GetInstance();

        mcp.AddTool("self.robot.move",
            "Call this tool immediately for a movement command. Never ask what the user wants. "
            "Reply in at most four words. direction: forward|backward|left|right|stop; "
            "speed: slow|medium|fast|turbo; duration_ms default 1500. A quarter turn is about 600 ms.",
            PropertyList({Property("direction", kPropertyTypeString),
                Property("speed", kPropertyTypeString, std::string("medium")),
                Property("duration_ms", kPropertyTypeInteger, 1500, 100, 10000)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeDir direction;
                MdeeSpeed speed = MdeeSpeed::kMedium;
                if (!MdeeRobot::ParseDir(p["direction"].value<std::string>(), direction)) return false;
                MdeeRobot::ParseSpeed(p["speed"].value<std::string>(), speed);
                robot.Move(direction, speed, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.robot.stop",
            "Immediately stop all wheel movement. Call immediately; reply in at most four words.",
            PropertyList(), [&robot](const PropertyList&) -> ReturnValue { robot.Stop(); return true; });

        mcp.AddTool("self.robot.set_mode",
            "Immediately set robot mode: manual, wander, or follow. Call now; reply in at most four words.",
            PropertyList({Property("mode", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeMode mode;
                if (!MdeeRobot::ParseMode(p["mode"].value<std::string>(), mode)) return false;
                robot.SetMode(mode);
                return true;
            });

        mcp.AddTool("self.robot.neck",
            "Immediately move the neck using pose values 0-100. turn 0=left, 50=center, 100=right; tilt 0=down, 50=level, 100=up. Reply in at most four words.",
            PropertyList({Property("turn", kPropertyTypeInteger, 50, 0, 100),
                          Property("tilt", kPropertyTypeInteger, 50, 0, 100)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                robot.Head(p["turn"].value<int>(), p["tilt"].value<int>());
                return true;
            });

        mcp.AddTool("self.robot.ears",
            "Immediately move both ears or one ear. gesture: up|down|wiggle|flick|neutral|auto. Reply in at most four words.",
            PropertyList({Property("gesture", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeEar gesture;
                if (!MdeeRobot::ParseEar(p["gesture"].value<std::string>(), gesture)) return false;
                robot.Ears(gesture);
                return true;
            });

        mcp.AddTool("self.robot.perform",
            "Immediately perform one currently supported short routine: hum_sway, tired_wobble, "
            "tongue_dance, cry_comfort, wiggle, spin, yawn, greet, nod_yes, shake_no. "
            "Reply in at most four words.",
            PropertyList({Property("name", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                robot.Perform(p["name"].value<std::string>());
                return true;
            });

        mcp.AddTool("self.robot.set_emotion",
            "Immediately set the robot's emotion. Call now; reply in at most four words.",
            PropertyList({Property("name", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                robot.SetEmotion(p["name"].value<std::string>());
                return true;
            });

        mcp.AddTool("self.robot.power",
            "Immediately run soft power-off or nap sequence. action: sleep|power_off|wake. Reply in at most four words.",
            PropertyList({Property("action", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                const std::string action = p["action"].value<std::string>();
                if (action == "power_off") robot.Power(true);
                else if (action == "sleep") robot.Power(false);
                else if (action == "wake") { robot.Ears(MdeeEar::kPerk); robot.Head(50, 50); }
                else return false;
                return true;
            });

        mcp.AddTool("self.robot.get_status",
            "Return current battery voltage and percent, front/rear distance, mode and wheel state.",
            PropertyList(), [&robot](const PropertyList&) -> ReturnValue { return robot.StatusJson(); });

        mcp.AddTool("self.lights.bars",
            "Immediately set the four-pixel light bars. effect: off|solid|breathe|rainbow|heartbeat|flash|chase|sparkle. r,g,b are 0-255.",
            PropertyList({Property("effect", kPropertyTypeString),
                Property("r", kPropertyTypeInteger, 0, 0, 255),
                Property("g", kPropertyTypeInteger, 216, 0, 255),
                Property("b", kPropertyTypeInteger, 255, 0, 255)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                const std::string e = p["effect"].value<std::string>();
                MdeeNeo effect = MdeeNeo::kSolid;
                if (e == "off") effect = MdeeNeo::kOff;
                else if (e == "breathe") effect = MdeeNeo::kBreathe;
                else if (e == "rainbow") effect = MdeeNeo::kRainbow;
                else if (e == "heartbeat") effect = MdeeNeo::kHeartbeat;
                else if (e == "flash") effect = MdeeNeo::kFlash;
                else if (e == "chase") effect = MdeeNeo::kChase;
                else if (e == "sparkle") effect = MdeeNeo::kSparkle;
                robot.SetNeo(effect, p["r"].value<int>(), p["g"].value<int>(), p["b"].value<int>());
                return true;
            });

        mcp.AddTool("self.lights.blue",
            "Immediately control the blue LEDs: off|on|breathe|blink|pulse. Reply in at most four words.",
            PropertyList({Property("mode", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                robot.SetBlueLed(p["mode"].value<std::string>().c_str());
                return true;
            });

        mcp.AddTool("self.lights.brightness",
            "Set light brightness. target bars|blue|screens|all; percent 0-100.",
            PropertyList({Property("target", kPropertyTypeString),
                          Property("percent", kPropertyTypeInteger, 65, 0, 100)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                const std::string target = p["target"].value<std::string>();
                const int percent = p["percent"].value<int>();
                robot.SetBrightness(target.c_str(), percent);
                if (target == "screens" || target == "all") {
                    Backlight* light = Board::GetInstance().GetBacklight();
                    if (light != nullptr) light->SetBrightness(static_cast<uint8_t>(percent));
                }
                return true;
            });

        mcp.AddTool("self.system.memory",
            "Report free internal heap, free PSRAM and largest internal free block.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                char result[192];
                snprintf(result, sizeof(result),
                    "{\"heap_free\":%u,\"psram_free\":%u,\"heap_largest\":%u}",
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
                return std::string(result);
            });

        mcp.AddTool("self.system.phone_api",
            "Return the phone control page address and its HTTP API endpoints.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif == nullptr) netif = esp_netif_get_default_netif();
                esp_netif_ip_info_t ip = {};
                char result[640];
                if (netif == nullptr || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
                    snprintf(result, sizeof(result), "{\"url\":\"Wi-Fi not connected\"}");
                } else {
                    snprintf(result, sizeof(result),
                        "{\"url\":\"http://" IPSTR "/\",\"endpoints\":[\"/api/status\",\"/api/drive\",\"/api/move\",\"/api/stop\",\"/api/mode\",\"/api/head\",\"/api/ears\",\"/api/perform\",\"/api/emotion\",\"/api/bars\",\"/api/neo\",\"/api/blue\",\"/api/brightness\",\"/api/power\"]}",
                        IP2STR(&ip.ip));
                }
                return std::string(result);
            });

        mcp.AddTool("self.system.status",
            "Report firmware version, uptime, Wi-Fi RSSI, IP address, battery, mode and sensor readings.",
            PropertyList(), [&robot](const PropertyList&) -> ReturnValue {
                wifi_ap_record_t ap = {};
                const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
                esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif == nullptr) netif = esp_netif_get_default_netif();
                esp_netif_ip_info_t ip = {};
                char ip_text[24] = "0.0.0.0";
                if (netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK)
                    snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&ip.ip));
                const esp_app_desc_t* app = esp_app_get_description();
                const std::string battery = robot.StatusJson();
                char result[768];
                snprintf(result, sizeof(result),
                    "{\"version\":\"%s\",\"uptime_s\":%lld,\"wifi_rssi\":%d,\"ip\":\"%s\",\"robot\":%s}",
                    app != nullptr ? app->version : "unknown",
                    static_cast<long long>(esp_timer_get_time() / 1000000), rssi, ip_text, battery.c_str());
                return std::string(result);
            });
    }

public:
    MdeeBoard() : boot_button_(BOOT_BUTTON_GPIO, false, 2000), backlight_(DISPLAY_BACKLIGHT_PIN) {
        MdeeRobot::GetInstance().Init();
        InitializeDisplays();
        InitializeButtons();
        RegisterTools();
        backlight_.SetBrightness(85);
        MdeeRobot::GetInstance().Wake();
        WebControl::StartWhenOnline();
        ESP_LOGI(TAG, "M'DEE single-board firmware ready");
    }

    Led* GetLed() override {
        static NoLed led;
        return &led;
    }
    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
        return &codec;
    }
    Display* GetDisplay() override { return face_display_; }
    Backlight* GetBacklight() override { return &backlight_; }
    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        const int pct = MdeeRobot::GetInstance().BatteryPercent();
        if (MdeeRobot::GetInstance().BatteryVolts() <= 0.1f) return false;
        level = pct;
        charging = false;
        discharging = true;
        return true;
    }
    void SetPowerSaveLevel(PowerSaveLevel) override {
        WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    }
};

DECLARE_BOARD(MdeeBoard);
