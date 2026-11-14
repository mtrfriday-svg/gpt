# File change manifest

This manifest compares the delivered project with the uploaded `Update-main.zip`.

## Changed files (22)

- `.github/workflows/mdee.yml`
- `.gitignore`
- `TERMUX_GUIDE.md`
- `main/CMakeLists.txt`
- `main/Kconfig.projbuild`
- `main/boards/common/board.cc`
- `main/boards/common/board.h`
- `main/boards/mdee-robot/config.h`
- `main/boards/mdee-robot/config.json`
- `main/boards/mdee-robot/face_display.cc`
- `main/boards/mdee-robot/face_display.h`
- `main/boards/mdee-robot/mdee_robot.cc`
- `main/boards/mdee-robot/mdee_robot.h`
- `main/boards/mdee-robot/mdee_robot_board.cc`
- `main/boards/mdee-robot/web_control.cc`
- `main/display/lvgl_display/jpg/image_to_jpeg.cpp`
- `main/display/lvgl_display/jpg/image_to_jpeg.h`
- `main/display/lvgl_display/jpg/jpeg_to_image.c`
- `main/idf_component.yml`
- `main/mcp_server.cc`
- `scripts/build.py`
- `workflow_backup/mdee.yml`

## Added files (6)

- `FUNCTIONS_GUIDE.md`
- `README.md`
- `WIRING_GUIDE.md`
- `main/boards/mdee-robot/font6x8.h`
- `main/boards/mdee-robot/status_display.cc`
- `main/boards/mdee-robot/status_display.h`

## Deleted files (32)

- `BATTERY_DIVIDER.md`
- `DESIGN_NOTES.md`
- `LOCAL_MUSIC_GUIDE.md`
- `LOGIC_BATTERY_DIVIDER.md`
- `MDEE_COMPLETE_GUIDE.md`
- `MDEE_README.md`
- `main/boards/common/adc_battery_monitor.cc`
- `main/boards/common/adc_battery_monitor.h`
- `main/boards/common/camera.h`
- `main/boards/common/esp32_camera.cc`
- `main/boards/common/esp32_camera.h`
- `main/boards/common/esp_video.cc`
- `main/boards/common/esp_video.h`
- `main/boards/mdee-robot/led_bar.cc`
- `main/boards/mdee-robot/led_bar.h`
- `main/boards/mdee-robot/logic_battery.cc`
- `main/boards/mdee-robot/logic_battery.h`
- `main/boards/mdee-robot/oled_font6x8.h`
- `main/boards/mdee-robot/oled_status.cc`
- `main/boards/mdee-robot/oled_status.h`
- `main/display/oled_display.cc`
- `main/display/oled_display.h`
- `mdee_body/CMakeLists.txt`
- `mdee_body/body_config.h`
- `mdee_body/main/CMakeLists.txt`
- `mdee_body/main/idf_component.yml`
- `mdee_body/main/link.cc`
- `mdee_body/main/link.h`
- `mdee_body/main/main.cc`
- `mdee_body/main/mdee_robot.cc`
- `mdee_body/main/mdee_robot.h`
- `mdee_body/sdkconfig.defaults`

The GitHub Actions workflow and its backup are identical. The build script exposes only the `mdee-robot` target.
