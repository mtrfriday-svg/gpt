#ifndef MDEE_ROBOT_H
#define MDEE_ROBOT_H

#include <atomic>
#include <cstdint>
#include <string>

enum class MdeeDir { kForward, kBackward, kLeft, kRight, kStop };
enum class MdeeSpeed { kSlow, kMedium, kFast, kTurbo };
enum class MdeeMode { kManual, kWander, kFollow };
enum class MdeeLights { kOff, kOn, kChase, kBlink, kSignalLeft, kSignalRight, kBatteryBar };
enum class MdeeNeo { kOff, kSolid, kBreathe, kRainbow, kHeartbeat, kFlash, kChase, kSparkle };
enum class MdeeEar { kAuto, kNeutral, kPerk, kDroop, kWiggle, kFlick };

class MdeeRobot {
public:
    static MdeeRobot& GetInstance();
    void Init();
    void Move(MdeeDir direction, MdeeSpeed speed, int duration_ms);
    void Drive(int left_percent, int right_percent, int hold_ms);
    void Stop();
    void SetMode(MdeeMode mode);
    MdeeMode GetMode() const { return mode_.load(); }
    static const char* ModeName(MdeeMode mode);
    static bool ParseMode(const std::string& name, MdeeMode& mode);
    static bool ParseSpeed(const std::string& name, MdeeSpeed& speed);
    static bool ParseDir(const std::string& name, MdeeDir& direction);
    static bool ParseEar(const std::string& name, MdeeEar& gesture);
    void SetServo(int index, int pose);
    void Head(int turn, int tilt);
    void Ears(MdeeEar gesture, int duration_ms = 0);
    void SetLights(MdeeLights pattern);
    void SetNeo(MdeeNeo effect, uint8_t red, uint8_t green, uint8_t blue);
    void SetBlueLed(const char* mode);
    void SetBrightness(const char* target, int percent);
    void SetEmotion(const std::string& emotion);
    void Perform(const std::string& name);
    void Power(bool soft_off);
    void Wake();
    int FrontCm() const { return front_cm_.load(); }
    int RearCm() const { return rear_cm_.load(); }
    float BatteryVolts() const { return battery_volts_.load(); }
    int BatteryPercent() const { return battery_percent_.load(); }
    bool LinkUp() const { return true; }
    std::string StatusJson();

private:
    MdeeRobot() = default;
    static void ControlTask(void* arg);
    static void SensorTask(void* arg);
    static void LightTask(void* arg);
    void InitMotors();
    void InitServos();
    void ApplyDrive(int left, int right);
    void SetServoPose(int index, int pose);
    void UpdateSensors();
    void UpdateBattery();
    void RenderLights();
    void SetPixel(int index, uint8_t r, uint8_t g, uint8_t b);

    std::atomic<int> target_left_{0}, target_right_{0};
    std::atomic<int> current_left_{0}, current_right_{0};
    std::atomic<int64_t> deadline_ms_{0};
    std::atomic<int> front_cm_{400}, rear_cm_{400};
    std::atomic<float> battery_volts_{0.0f};
    std::atomic<int> battery_percent_{0};
    std::atomic<MdeeMode> mode_{MdeeMode::kManual};
    std::atomic<bool> initialized_{false};
    std::atomic<int> servo_pose_[4];
    std::atomic<int> left_ear_pose_{0}, right_ear_pose_{0};
    std::atomic<int> led_brightness_{65}, blue_brightness_{60};
    std::atomic<int> led_effect_{0}, led_r_{0}, led_g_{216}, led_b_{255};
    std::atomic<int> blue_mode_{0};
    std::atomic<int> emotion_code_{0};
    int64_t low_voltage_since_ms_ = 0;
    std::atomic<bool> cutoff_active_{false};
};

#endif
