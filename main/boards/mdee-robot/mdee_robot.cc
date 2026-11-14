#include "mdee_robot.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/mcpwm_prelude.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_idf_version.h>
#include "board.h"
#include "display/display.h"
#include "status_display.h"
#include <led_strip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MdeeRobot"
namespace {
constexpr uint32_t kPwmResolution = 20000000;
constexpr uint32_t kPwmPeriod = 1000;
constexpr int kServoCount = 4;
constexpr gpio_num_t kServoPins[kServoCount] = {
    SERVO_NECK_TURN_PIN, SERVO_NECK_TILT_PIN, SERVO_LEFT_EAR_PIN, SERVO_RIGHT_EAR_PIN
};
constexpr ledc_channel_t kServoChannels[kServoCount] = {
    LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3, LEDC_CHANNEL_4
};
constexpr bool kServoEnabled[kServoCount] = {
    MDEE_SERVO_NECK_TURN_EN != 0, MDEE_SERVO_NECK_TILT_EN != 0,
    MDEE_SERVO_LEFT_EAR_EN != 0, MDEE_SERVO_RIGHT_EAR_EN != 0
};
constexpr int kPose0[kServoCount] = {
    SERVO_NECK_TURN_0, SERVO_NECK_TILT_0, SERVO_LEFT_EAR_0, SERVO_RIGHT_EAR_0
};
constexpr int kPose100[kServoCount] = {
    SERVO_NECK_TURN_100, SERVO_NECK_TILT_100, SERVO_LEFT_EAR_100, SERVO_RIGHT_EAR_100
};
mcpwm_timer_handle_t s_motor_timer = nullptr;
mcpwm_cmpr_handle_t s_motor_cmp[4] = {};
mcpwm_gen_handle_t s_motor_gen[4] = {};
adc_oneshot_unit_handle_t s_adc = nullptr;
adc_cali_handle_t s_adc_cali = nullptr;
led_strip_handle_t s_strip = nullptr;
int64_t NowMs() { return esp_timer_get_time() / 1000; }
int Clamp(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

int BatteryPercentFromCell(float cell) {
    if (cell >= 4.20f) return 100;
    if (cell >= 4.00f) return 80 + static_cast<int>((cell - 4.00f) * 100.0f);
    if (cell >= 3.85f) return 55 + static_cast<int>((cell - 3.85f) * (25.0f / 0.15f));
    if (cell >= 3.75f) return 35 + static_cast<int>((cell - 3.75f) * 200.0f);
    if (cell >= 3.65f) return 20 + static_cast<int>((cell - 3.65f) * 150.0f);
    if (cell >= 3.50f) return 8 + static_cast<int>((cell - 3.50f) * 80.0f);
    if (cell >= 3.30f) return static_cast<int>((cell - 3.30f) * 40.0f);
    return 0;
}
}

MdeeRobot& MdeeRobot::GetInstance() {
    static MdeeRobot instance;
    return instance;
}

void MdeeRobot::Init() {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) return;

    // Make every motor input low before initializing any PWM peripheral.
    const gpio_num_t motor_pins[] = {MOTOR_LEFT_IN1, MOTOR_LEFT_IN2, MOTOR_RIGHT_IN1, MOTOR_RIGHT_IN2};
    for (gpio_num_t pin : motor_pins) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pin;
        io.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_set_level(pin, 0));
    }

    gpio_config_t sensor_io = {};
    sensor_io.pin_bit_mask = (1ULL << ULTRASONIC_TRIG_PIN);
    sensor_io.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&sensor_io));
    ESP_ERROR_CHECK(gpio_set_level(ULTRASONIC_TRIG_PIN, 0));
    gpio_config_t echo_io = {};
    echo_io.pin_bit_mask = (1ULL << ULTRASONIC_FRONT_ECHO_PIN) | (1ULL << ULTRASONIC_REAR_ECHO_PIN);
    echo_io.mode = GPIO_MODE_INPUT;
    echo_io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    echo_io.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&echo_io));

    gpio_config_t blue_io = {};
    blue_io.pin_bit_mask = 1ULL << BLUE_LED_PIN;
    blue_io.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&blue_io));
    gpio_set_level(BLUE_LED_PIN, 0);

    InitMotors();
    InitServos();

    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = NEOPIXEL_PIN;
    strip_cfg.max_leds = NEOPIXEL_COUNT;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_cfg.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.resolution_hz = 10 * 1000 * 1000;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    strip_ = s_strip;
    led_strip_clear(s_strip);

    adc_oneshot_unit_init_cfg_t adc_cfg = {};
    adc_cfg.unit_id = BATTERY_ADC_UNIT;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc));
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg));
    adc_cali_curve_fitting_config_t cal_cfg = {};
    cal_cfg.unit_id = BATTERY_ADC_UNIT;
    cal_cfg.chan = BATTERY_ADC_CHANNEL;
    cal_cfg.atten = ADC_ATTEN_DB_12;
    cal_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cal_cfg, &s_adc_cali) != ESP_OK) s_adc_cali = nullptr;

    xTaskCreate(ControlTask, "mdee_control", 4096, this, 5, nullptr);
    xTaskCreate(SensorTask, "mdee_sensors", 4096, this, 4, nullptr);
    xTaskCreate(LightTask, "mdee_lights", 3072, this, 2, nullptr);
    ESP_LOGI(TAG, "Single-board robot hardware initialized");
}

void MdeeRobot::InitMotors() {
    mcpwm_timer_config_t timer_cfg = {};
    timer_cfg.group_id = 0;
    timer_cfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_cfg.resolution_hz = kPwmResolution;
    timer_cfg.period_ticks = kPwmPeriod;
    timer_cfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &s_motor_timer));

    mcpwm_oper_handle_t op[2] = {};
    for (int i = 0; i < 2; ++i) {
        mcpwm_operator_config_t op_cfg = {};
        op_cfg.group_id = 0;
        ESP_ERROR_CHECK(mcpwm_new_operator(&op_cfg, &op[i]));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(op[i], s_motor_timer));
    }
    const gpio_num_t pins[4] = {MOTOR_LEFT_IN1, MOTOR_LEFT_IN2, MOTOR_RIGHT_IN1, MOTOR_RIGHT_IN2};
    for (int i = 0; i < 4; ++i) {
        mcpwm_comparator_config_t cmp_cfg = {};
        cmp_cfg.flags.update_cmp_on_tez = true;
        ESP_ERROR_CHECK(mcpwm_new_comparator(op[i / 2], &cmp_cfg, &s_motor_cmp[i]));
        mcpwm_generator_config_t gen_cfg = {};
        gen_cfg.gen_gpio_num = pins[i];
        ESP_ERROR_CHECK(mcpwm_new_generator(op[i / 2], &gen_cfg, &s_motor_gen[i]));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
            s_motor_gen[i], MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
            s_motor_gen[i], MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            s_motor_cmp[i], MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_force_level(s_motor_gen[i], 0, true));
    }
    ESP_ERROR_CHECK(mcpwm_timer_enable(s_motor_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_motor_timer, MCPWM_TIMER_START_NO_STOP));
    ApplyDrive(0, 0);
}

void MdeeRobot::InitServos() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_14_BIT;
    timer.timer_num = LEDC_TIMER_1;
    timer.freq_hz = SERVO_PWM_HZ;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    for (int i = 0; i < kServoCount; ++i) {
        servo_pose_[i] = 50;
        if (!kServoEnabled[i]) continue;
        ledc_channel_config_t channel = {};
        channel.gpio_num = kServoPins[i];
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = kServoChannels[i];
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = LEDC_TIMER_1;
        channel.duty = 0;
        channel.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
        SetServoPose(i, i == 0 ? 50 : 0);
    }
    // Blue status LED uses timer 2 / channel 5; timer 0 / channel 0 remains for LCD backlight.
    ledc_timer_config_t blue_timer = {};
    blue_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    blue_timer.duty_resolution = LEDC_TIMER_10_BIT;
    blue_timer.timer_num = LEDC_TIMER_2;
    blue_timer.freq_hz = 1000;
    blue_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&blue_timer));
    ledc_channel_config_t blue = {};
    blue.gpio_num = BLUE_LED_PIN;
    blue.speed_mode = LEDC_LOW_SPEED_MODE;
    blue.channel = LEDC_CHANNEL_5;
    blue.intr_type = LEDC_INTR_DISABLE;
    blue.timer_sel = LEDC_TIMER_2;
    blue.duty = 0;
    blue.hpoint = 0;
    // GPIO is already configured as output; LEDC takes ownership here.
    ESP_ERROR_CHECK(ledc_channel_config(&blue));
}

void MdeeRobot::SetServoPose(int index, int pose) {
    if (index < 0 || index >= kServoCount || !kServoEnabled[index]) return;
    pose = Clamp(pose, 0, 100);
    int angle = kPose0[index] + (kPose100[index] - kPose0[index]) * pose / 100;
    angle = Clamp(angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
    const uint32_t pulse = SERVO_MIN_PULSE_US +
        static_cast<uint32_t>((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle / 180);
    const uint32_t duty = pulse * 50U * (1U << 14) / 1000000U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kServoChannels[index], duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kServoChannels[index]);
    servo_pose_[index] = pose;
}

void MdeeRobot::SetServo(int index, int pose) { SetServoPose(index, pose); }

void MdeeRobot::Head(int turn, int tilt) {
    SetServoPose(0, turn);
    SetServoPose(1, tilt);
}

void MdeeRobot::Ears(MdeeEar gesture, int) {
    int left = 50, right = 50;
    switch (gesture) {
        case MdeeEar::kAuto:
        case MdeeEar::kNeutral: left = 50; right = 50; break;
        case MdeeEar::kPerk: left = 100; right = 100; break;
        case MdeeEar::kDroop: left = 0; right = 0; break;
        case MdeeEar::kWiggle: left = 85; right = 15; break;
        case MdeeEar::kFlick: left = 100; right = 35; break;
    }
    SetServoPose(2, left);
    SetServoPose(3, right);
    left_ear_pose_ = left;
    right_ear_pose_ = right;
}

void MdeeRobot::ApplyDrive(int left, int right) {
    const int values[2] = {Clamp(left, -100, 100), Clamp(right, -100, 100)};
    for (int motor = 0; motor < 2; ++motor) {
        int v = values[motor];
        const int a = motor * 2;
        const int b = a + 1;
        if (v == 0) {
            mcpwm_generator_set_force_level(s_motor_gen[a], 0, true);
            mcpwm_generator_set_force_level(s_motor_gen[b], 0, true);
            continue;
        }
        const int duty = std::abs(v);
        const int active = v > 0 ? a : b;
        const int inactive = v > 0 ? b : a;
        if (duty >= 100) {
            mcpwm_generator_set_force_level(s_motor_gen[active], 1, true);
        } else {
            const uint32_t compare = static_cast<uint32_t>(kPwmPeriod * (100 - duty) / 100);
            mcpwm_comparator_set_compare_value(s_motor_cmp[active], compare);
            mcpwm_generator_set_force_level(s_motor_gen[active], -1, false);
        }
        mcpwm_generator_set_force_level(s_motor_gen[inactive], 0, true);
    }
}

void MdeeRobot::Drive(int left, int right, int hold_ms) {
    mode_ = MdeeMode::kManual;
    if (cutoff_active_.load()) { Stop(); return; }
    target_left_ = Clamp(left, -100, 100);
    target_right_ = Clamp(right, -100, 100);
    const int hold = hold_ms <= 0 ? MOTOR_DEADMAN_MS : hold_ms;
    deadline_ms_ = NowMs() + Clamp(hold, 1, 10000);
}

void MdeeRobot::Move(MdeeDir dir, MdeeSpeed speed, int duration_ms) {
    int pct = MOTOR_MEDIUM_PCT;
    switch (speed) {
        case MdeeSpeed::kSlow: pct = MOTOR_SLOW_PCT; break;
        case MdeeSpeed::kMedium: pct = MOTOR_MEDIUM_PCT; break;
        case MdeeSpeed::kFast: pct = MOTOR_FAST_PCT; break;
        case MdeeSpeed::kTurbo: pct = MOTOR_TURBO_PCT; break;
    }
    switch (dir) {
        case MdeeDir::kForward: Drive(pct, pct, duration_ms); break;
        case MdeeDir::kBackward: Drive(-pct, -pct, duration_ms); break;
        case MdeeDir::kLeft: Drive(-pct, pct, duration_ms); break;
        case MdeeDir::kRight: Drive(pct, -pct, duration_ms); break;
        case MdeeDir::kStop: Stop(); break;
    }
}

void MdeeRobot::Stop() {
    target_left_ = 0;
    target_right_ = 0;
    current_left_ = 0;
    current_right_ = 0;
    deadline_ms_ = 0;
    mode_ = MdeeMode::kManual;
    ApplyDrive(0, 0);
}

void MdeeRobot::SetMode(MdeeMode mode) {
    mode_ = mode;
    target_left_ = 0;
    target_right_ = 0;
    deadline_ms_ = 0;
}

const char* MdeeRobot::ModeName(MdeeMode mode) {
    switch (mode) {
        case MdeeMode::kManual: return "manual";
        case MdeeMode::kWander: return "wander";
        case MdeeMode::kFollow: return "follow";
    }
    return "manual";
}

bool MdeeRobot::ParseMode(const std::string& n, MdeeMode& out) {
    if (n == "manual") out = MdeeMode::kManual;
    else if (n == "wander" || n == "explore" || n == "roam") out = MdeeMode::kWander;
    else if (n == "follow") out = MdeeMode::kFollow;
    else return false;
    return true;
}

bool MdeeRobot::ParseSpeed(const std::string& n, MdeeSpeed& out) {
    if (n == "slow" || n == "slowly") out = MdeeSpeed::kSlow;
    else if (n == "medium" || n == "normal") out = MdeeSpeed::kMedium;
    else if (n == "fast" || n == "quickly") out = MdeeSpeed::kFast;
    else if (n == "turbo" || n == "high" || n == "high_speed") out = MdeeSpeed::kTurbo;
    else return false;
    return true;
}

bool MdeeRobot::ParseDir(const std::string& n, MdeeDir& out) {
    if (n == "forward" || n == "go forward" || n == "move forward") out = MdeeDir::kForward;
    else if (n == "back" || n == "backward" || n == "reverse") out = MdeeDir::kBackward;
    else if (n == "left" || n == "turn left") out = MdeeDir::kLeft;
    else if (n == "right" || n == "turn right") out = MdeeDir::kRight;
    else if (n == "stop" || n == "halt") out = MdeeDir::kStop;
    else return false;
    return true;
}

bool MdeeRobot::ParseEar(const std::string& n, MdeeEar& out) {
    if (n == "auto") out = MdeeEar::kAuto;
    else if (n == "neutral") out = MdeeEar::kNeutral;
    else if (n == "up" || n == "perk") out = MdeeEar::kPerk;
    else if (n == "down" || n == "droop") out = MdeeEar::kDroop;
    else if (n == "wiggle") out = MdeeEar::kWiggle;
    else if (n == "flick") out = MdeeEar::kFlick;
    else return false;
    return true;
}

void MdeeRobot::SetLights(MdeeLights pattern) {
    int effect = 0;
    if (pattern == MdeeLights::kOn) effect = 1;
    else if (pattern == MdeeLights::kChase) effect = 2;
    else if (pattern == MdeeLights::kBlink) effect = 3;
    else if (pattern == MdeeLights::kSignalLeft) effect = 4;
    else if (pattern == MdeeLights::kSignalRight) effect = 5;
    else if (pattern == MdeeLights::kBatteryBar) effect = 6;
    led_effect_ = effect;
}

void MdeeRobot::SetNeo(MdeeNeo effect, uint8_t r, uint8_t g, uint8_t b) {
    led_effect_ = static_cast<int>(effect) + 10;
    led_r_ = r; led_g_ = g; led_b_ = b;
}

void MdeeRobot::SetBlueLed(const char* mode) {
    if (strcmp(mode, "on") == 0) blue_mode_ = 1;
    else if (strcmp(mode, "breathe") == 0) blue_mode_ = 2;
    else if (strcmp(mode, "blink") == 0) blue_mode_ = 3;
    else if (strcmp(mode, "pulse") == 0) blue_mode_ = 4;
    else blue_mode_ = 0;
}

void MdeeRobot::SetBrightness(const char* target, int percent) {
    const int value = Clamp(percent, 0, 100);
    if (strcmp(target, "bars") == 0 || strcmp(target, "all") == 0) led_brightness_ = value;
    if (strcmp(target, "blue") == 0 || strcmp(target, "all") == 0) blue_brightness_ = value;
}

void MdeeRobot::SetEmotion(const std::string& emotion) {
    Display* display = Board::GetInstance().GetDisplay();
    if (display != nullptr) display->SetEmotion(emotion.c_str());
    int code = 0;
    if (emotion == "happy" || emotion == "excited" || emotion == "laughing") code = 1;
    else if (emotion == "sad" || emotion == "sleepy" || emotion == "tired") code = 2;
    else if (emotion == "surprised" || emotion == "shocked") code = 3;
    else if (emotion == "angry") code = 4;
    else if (emotion == "thinking" || emotion == "confused") code = 5;
    emotion_code_ = code;
    if (code == 1) Ears(MdeeEar::kWiggle);
    else if (code == 2 || code == 4) Ears(MdeeEar::kDroop);
    else if (code == 3 || code == 5) Ears(MdeeEar::kPerk);
}

void MdeeRobot::Perform(const std::string& name) {
    if (name == "stop") { Stop(); return; }
    if (name == "spin") { Move(MdeeDir::kLeft, MdeeSpeed::kFast, 1200); }
    else if (name == "wiggle" || name == "tongue_dance") {
        Ears(MdeeEar::kWiggle); Move(MdeeDir::kLeft, MdeeSpeed::kSlow, 300);
    } else if (name == "yawn" || name == "tired_wobble") {
        Ears(MdeeEar::kDroop); Head(50, 25);
    } else if (name == "greet" || name == "excited_dash") {
        Ears(MdeeEar::kPerk); Move(MdeeDir::kForward, MdeeSpeed::kSlow, 300);
    } else if (name == "hum_sway") {
        Head(35, 50); Move(MdeeDir::kLeft, MdeeSpeed::kSlow, 250);
    } else if (name == "cry_comfort") {
        Ears(MdeeEar::kDroop); SetNeo(MdeeNeo::kBreathe, 0, 30, 120);
    } else if (name == "nod_yes") {
        Head(50, 75);
    } else if (name == "shake_no") {
        Head(25, 50);
    }
}

namespace {
void WakeSequenceTask(void*) {
    auto& robot = MdeeRobot::GetInstance();
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int pose = 0; pose <= 100; pose += 4) {
        robot.SetServo(2, pose);
        robot.SetServo(3, pose);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    for (int pose = 0; pose <= 100; pose += 4) {
        robot.SetServo(1, pose);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    robot.SetServo(0, 50);
    robot.SetNeo(MdeeNeo::kBreathe, 0, 120, 180);
    robot.SetBlueLed("breathe");
    vTaskDelete(nullptr);
}

void PowerOffSequenceTask(void*) {
    auto& robot = MdeeRobot::GetInstance();
    robot.Stop();
    if (Board::GetInstance().GetDisplay() != nullptr)
        Board::GetInstance().GetDisplay()->SetEmotion("sleepy");
    if (auto* status = StatusDisplay::GetInstance()) status->SetState("Goodnight");
    robot.Ears(MdeeEar::kDroop);
    robot.Head(50, 0);
    robot.SetNeo(MdeeNeo::kBreathe, 0, 25, 55);
    robot.SetBlueLed("breathe");
    for (int i = 65; i >= 0; i -= 5) {
        robot.SetBrightness("bars", i);
        robot.SetBrightness("blue", i);
        Backlight* fade_backlight = Board::GetInstance().GetBacklight();
        if (fade_backlight != nullptr) fade_backlight->SetBrightness(static_cast<uint8_t>(i));
        vTaskDelay(pdMS_TO_TICKS(180));
    }
    vTaskDelay(pdMS_TO_TICKS(350));
    for (int channel = 1; channel <= 4; ++channel)
        ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel), 0);
    robot.SetNeo(MdeeNeo::kOff, 0, 0, 0);
    robot.SetBlueLed("off");
    if (auto* status = StatusDisplay::GetInstance()) status->Sleep();
    Backlight* backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr) backlight->SetBrightness(0);
    gpio_config_t boot = {};
    boot.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    boot.mode = GPIO_MODE_INPUT;
    boot.pull_up_en = GPIO_PULLUP_ENABLE;
    boot.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&boot);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_sleep_enable_ext1_wakeup_io(1ULL << BOOT_BUTTON_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup(1ULL << BOOT_BUTTON_GPIO, ESP_EXT1_WAKEUP_ALL_LOW);
#endif
    esp_deep_sleep_start();
    vTaskDelete(nullptr);
}
}

void MdeeRobot::Wake() {
    if (xTaskCreate(WakeSequenceTask, "mdee_wakeup", 3072, nullptr, 3, nullptr) != pdPASS)
        ESP_LOGE(TAG, "Could not start wake-up sequence");
}

void MdeeRobot::Power(bool soft_off) {
    Stop();
    if (!soft_off) {
        SetEmotion("sleepy");
        Ears(MdeeEar::kDroop);
        Head(50, 0);
        SetNeo(MdeeNeo::kBreathe, 0, 30, 80);
        return;
    }
    if (xTaskCreate(PowerOffSequenceTask, "mdee_poweroff", 4096, nullptr, 5, nullptr) != pdPASS)
        ESP_LOGE(TAG, "Could not start power-off sequence");
}

void MdeeRobot::SetPixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (s_strip == nullptr) return;
    led_strip_set_pixel(s_strip, index, r, g, b);
}

void MdeeRobot::RenderLights() {
    if (s_strip == nullptr) return;
    const int effect = led_effect_.load();
    const int64_t tick = NowMs() / 100;
    const int bright = led_brightness_.load();
    uint8_t r = static_cast<uint8_t>(led_r_.load());
    uint8_t g = static_cast<uint8_t>(led_g_.load());
    uint8_t b = static_cast<uint8_t>(led_b_.load());
    int legacy = effect;
    int neo = -1;
    if (effect >= 10) { neo = effect - 10; legacy = -1; }

    if (legacy >= 0) {
        if (legacy == 0) r = g = b = 0;
        else if (legacy == 1) { r = 255; g = 255; b = 255; }
        else if ((legacy == 3 || legacy == 4 || legacy == 5) && (tick & 1) == 0) r = g = b = 0;
        else if (legacy == 6) {
            const int count = Clamp((battery_percent_.load() * 4 + 99) / 100, 0, 4);
            for (int i = 0; i < 4; ++i) SetPixel(i, 0, i < count ? 180 : 0, 0);
            led_strip_refresh(s_strip);
            return;
        }
    } else {
        switch (neo) {
            case 0: r = g = b = 0; break;
            case 1: break;
            case 2: {
                const float wave = static_cast<float>(0.25 + 0.75 * (0.5 + 0.5 * sin(tick * 0.12)));
                r = static_cast<uint8_t>(r * wave); g = static_cast<uint8_t>(g * wave); b = static_cast<uint8_t>(b * wave);
                break;
            }
            case 3: {
                const uint8_t hue = static_cast<uint8_t>((tick * 5) & 0xff);
                r = static_cast<uint8_t>(hue < 85 ? 255 - hue * 3 : (hue > 170 ? hue * 3 - 510 : 0));
                g = static_cast<uint8_t>(hue >= 85 && hue < 170 ? (hue - 85) * 3 : 0);
                b = static_cast<uint8_t>(hue >= 170 ? (hue - 170) * 3 : (85 - hue) * 3);
                break;
            }
            case 4:
                if ((tick % 10) >= 3 && (tick % 10) != 5) r = g = b = 0;
                break;
            case 5:
                if ((tick % 8) >= 4) r = g = b = 0;
                break;
            case 6: case 7: break;
            default: r = g = b = 0; break;
        }
    }

    for (int i = 0; i < NEOPIXEL_COUNT; ++i) {
        int pr = r, pg = g, pb = b;
        if ((legacy == 2 || neo == 6) && i != static_cast<int>(tick % NEOPIXEL_COUNT)) {
            pr = pg = pb = 0;
        } else if (neo == 7 && ((tick + i * 3) % 7) != 0) {
            pr = pg = pb = 0;
        } else if ((legacy == 4 && i >= 2) || (legacy == 5 && i < 2)) {
            pr = pg = pb = 0;
        }
        SetPixel(i, static_cast<uint8_t>(pr * bright / 100),
                    static_cast<uint8_t>(pg * bright / 100),
                    static_cast<uint8_t>(pb * bright / 100));
    }
    led_strip_refresh(s_strip);

    int blue = 0;
    switch (blue_mode_.load()) {
        case 1: blue = 1023; break;
        case 2: blue = static_cast<int>((0.5 + 0.5 * sin(static_cast<double>(tick) * 0.12)) * 1023); break;
        case 3: blue = (tick % 10) < 5 ? 1023 : 0; break;
        case 4: blue = (tick % 8) < 2 ? 1023 : 0; break;
        default: blue = 0; break;
    }
    blue = blue * blue_brightness_.load() / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, static_cast<uint32_t>(blue));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5);
}

void MdeeRobot::UpdateBattery() {
    if (s_adc == nullptr) return;
    int total_mv = 0;
    int valid = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) continue;
        int mv = 0;
        if (s_adc_cali == nullptr || adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK)
            mv = raw * 3100 / 4095;
        total_mv += mv;
        ++valid;
    }
    if (valid == 0) return;
    const float pin_v = static_cast<float>(total_mv) / (1000.0f * valid);
    const float measured_pack_v = pin_v * (BATTERY_R_UPPER_OHM + BATTERY_R_LOWER_OHM) /
                                  BATTERY_R_LOWER_OHM * BATTERY_CAL_FACTOR;
    static float filtered_pack_v = 0.0f;
    if (measured_pack_v < BATTERY_NO_PACK_VOLTS || measured_pack_v > 9.5f) {
        filtered_pack_v = 0.0f;
        battery_volts_ = 0.0f;
        battery_percent_ = 0;
        return;
    }
    filtered_pack_v = filtered_pack_v > 0.1f
        ? filtered_pack_v * 0.85f + measured_pack_v * 0.15f : measured_pack_v;
    battery_volts_ = filtered_pack_v;
    battery_percent_ = Clamp(BatteryPercentFromCell(filtered_pack_v / 2.0f), 0, 100);
}

void MdeeRobot::UpdateSensors() {
    auto measure = [](gpio_num_t echo, uint32_t timeout_us) -> int {
        gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
        esp_rom_delay_us(3);
        gpio_set_level(ULTRASONIC_TRIG_PIN, 1);
        esp_rom_delay_us(10);
        gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
        const int64_t start_wait = esp_timer_get_time();
        while (gpio_get_level(echo) == 0) {
            if (esp_timer_get_time() - start_wait > timeout_us) return ULTRASONIC_MAX_CM;
        }
        const int64_t start = esp_timer_get_time();
        while (gpio_get_level(echo) != 0) {
            if (esp_timer_get_time() - start > timeout_us) return ULTRASONIC_MAX_CM;
        }
        const int64_t duration = esp_timer_get_time() - start;
        const int cm = static_cast<int>(duration / 58);
        return (cm <= 0 || cm > ULTRASONIC_MAX_CM) ? ULTRASONIC_MAX_CM : cm;
    };
    int f[3] = {ULTRASONIC_MAX_CM, ULTRASONIC_MAX_CM, ULTRASONIC_MAX_CM};
    int r[3] = {ULTRASONIC_MAX_CM, ULTRASONIC_MAX_CM, ULTRASONIC_MAX_CM};
    for (int i = 0; i < 3; ++i) {
        f[i] = measure(ULTRASONIC_FRONT_ECHO_PIN, 15000);
        vTaskDelay(pdMS_TO_TICKS(8));
        r[i] = measure(ULTRASONIC_REAR_ECHO_PIN, 6000);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    std::sort(f, f + 3);
    std::sort(r, r + 3);
    front_cm_ = f[1];
    rear_cm_ = r[1];
}

void MdeeRobot::ControlTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    int64_t last_cutoff_check = 0;
    int64_t last_autonomy_action = 0;
    int64_t low_battery_since_ms = 0;
    bool low_battery_alert_started = false;
    while (true) {
        const int64_t now = NowMs();
        if (self->deadline_ms_.load() > 0 && now >= self->deadline_ms_.load()) {
            self->target_left_ = 0;
            self->target_right_ = 0;
            self->deadline_ms_ = 0;
        }
        if (self->mode_.load() == MdeeMode::kWander && now - last_autonomy_action > 250) {
            const int front = self->front_cm_.load();
            const int rear = self->rear_cm_.load();
            if (front < 24) {
                if (rear > 28) { self->target_left_ = -MOTOR_MEDIUM_PCT; self->target_right_ = -MOTOR_MEDIUM_PCT; }
                else { self->target_left_ = MOTOR_MEDIUM_PCT; self->target_right_ = -MOTOR_MEDIUM_PCT; }
                self->deadline_ms_ = now + 500;
                last_autonomy_action = now;
            } else if (self->deadline_ms_.load() == 0) {
                self->target_left_ = MOTOR_MEDIUM_PCT;
                self->target_right_ = MOTOR_MEDIUM_PCT;
                self->deadline_ms_ = now + 400;
            }
        } else if (self->mode_.load() == MdeeMode::kFollow && now - last_autonomy_action > 180) {
            const int front = self->front_cm_.load();
            const int rear = self->rear_cm_.load();
            if (front >= 40 && front <= 90) self->target_left_ = self->target_right_ = MOTOR_SLOW_PCT;
            else if (front < 22 && rear > 28) self->target_left_ = self->target_right_ = -MOTOR_SLOW_PCT;
            else self->target_left_ = self->target_right_ = 0;
            self->deadline_ms_ = now + 220;
            last_autonomy_action = now;
        }
        const float pack_v = self->battery_volts_.load();
#if MOTOR_SOFT_CUTOFF_ENABLE
        if (pack_v > 0.1f && pack_v < MOTOR_CUTOFF_VOLTS) {
            if (self->low_voltage_since_ms_ == 0) self->low_voltage_since_ms_ = now;
            if (now - self->low_voltage_since_ms_ >= MOTOR_CUTOFF_HOLD_MS) {
                self->cutoff_active_ = true;
                self->target_left_ = self->target_right_ = 0;
            }
        } else {
            self->low_voltage_since_ms_ = 0;
            if (pack_v >= MOTOR_CUTOFF_VOLTS + 0.2f) self->cutoff_active_ = false;
        }
#endif
        static int64_t critical_since_ms = 0;
        static bool critical_poweroff_started = false;
        if (pack_v > 0.1f && pack_v < BATTERY_CRITICAL_VOLTS) {
            if (critical_since_ms == 0) critical_since_ms = now;
            if (!critical_poweroff_started && now - critical_since_ms >= 10000) {
                critical_poweroff_started = true;
                self->Power(true);
            }
        } else {
            critical_since_ms = 0;
            if (pack_v >= BATTERY_CRITICAL_VOLTS + 0.2f) critical_poweroff_started = false;
        }
        if (now - last_cutoff_check > 500) {
            self->UpdateBattery();
            last_cutoff_check = now;
        }
        const int battery_pct = self->battery_percent_.load();
        if (self->battery_volts_.load() > BATTERY_NO_PACK_VOLTS && battery_pct <= BATTERY_LOW_PERCENT) {
            if (low_battery_since_ms == 0) low_battery_since_ms = now;
            if (!low_battery_alert_started && now - low_battery_since_ms >= 5000) {
                low_battery_alert_started = true;
                self->SetEmotion("sad");
                self->SetNeo(MdeeNeo::kBreathe, 220, 0, 0);
                self->SetBlueLed("blink");
            }
        } else {
            low_battery_since_ms = 0;
            if (battery_pct > BATTERY_LOW_PERCENT + 8) low_battery_alert_started = false;
        }

        int left_target = self->target_left_.load();
        int right_target = self->target_right_.load();
        const int front = self->front_cm_.load();
        const int rear = self->rear_cm_.load();
        if (left_target > 0 && right_target > 0) {
            if (front < ULTRASONIC_FRONT_STOP_CM) left_target = right_target = 0;
            else if (front < ULTRASONIC_FRONT_SLOW_CM) {
                left_target /= 2;
                right_target /= 2;
            }
        } else if (left_target < 0 && right_target < 0 && rear < ULTRASONIC_FRONT_STOP_CM) {
            left_target = right_target = 0;
        }
        auto ramp = [](int current, int target) {
            if (target == 0) return 0;
            if (current < target) return std::min(current + MOTOR_RAMP_PERCENT, target);
            if (current > target) return std::max(current - MOTOR_RAMP_PERCENT, target);
            return current;
        };
        int left = ramp(self->current_left_.load(), left_target);
        int right = ramp(self->current_right_.load(), right_target);
        if (self->cutoff_active_.load()) left = right = 0;

        // Convert requested duty to a motor-voltage ceiling based on the measured pack.
        const float supply = pack_v > 4.0f ? pack_v : 7.4f;
        auto to_duty = [supply](int speed_pct) {
            if (speed_pct == 0) return 0;
            const float magnitude = static_cast<float>(std::abs(speed_pct)) / 100.0f;
            const float requested_volts = MOTOR_MIN_START_VOLTS +
                magnitude * (MOTOR_MAX_VOLTS - MOTOR_MIN_START_VOLTS);
            const int duty = Clamp(static_cast<int>(requested_volts * 100.0f / supply), 0, 100);
            return speed_pct < 0 ? -duty : duty;
        };
        self->current_left_ = left;
        self->current_right_ = right;
        self->ApplyDrive(to_duty(left), to_duty(right));
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_INTERVAL_MS));
    }
}

void MdeeRobot::SensorTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    while (true) {
        self->UpdateSensors();
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

void MdeeRobot::LightTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    while (true) {
        self->RenderLights();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

std::string MdeeRobot::StatusJson() {
    char buf[512];
    const float volts = battery_volts_.load();
    const int pct = battery_percent_.load();
    const int n = snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"front_cm\":%d,\"rear_cm\":%d,\"battery_v\":%.2f,"
        "\"battery_pct\":%d,\"battery_cutoff\":%s,\"left\":%d,\"right\":%d,"
        "\"neck_turn\":%d,\"neck_tilt\":%d,\"left_ear\":%d,\"right_ear\":%d}",
        ModeName(mode_.load()), front_cm_.load(), rear_cm_.load(), volts, pct,
        cutoff_active_.load() ? "true" : "false", current_left_.load(), current_right_.load(),
        servo_pose_[0].load(), servo_pose_[1].load(), left_ear_pose_.load(), right_ear_pose_.load());
    if (n < 0) return "{}";
    buf[sizeof(buf) - 1] = '\0';
    return std::string(buf);
}
