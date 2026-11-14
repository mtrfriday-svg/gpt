# M'DEE functions and user guide

This guide describes the code currently present in this single-board firmware package. Some behaviours in the design brief are still future work; they are identified as such rather than described as completed.

## 1. What the firmware is

- One ESP32-S3 N16R8 controller runs XiaoZhi voice firmware and M'DEE robot control.
- The XiaoZhi voice stack, Wi-Fi provisioning, wake-word pipeline and phone web-control server remain part of the project.
- Robot motors, sensors, servos, displays and lights are connected directly to the ESP32-S3.
- This is a one-controller design with no camera or video features, no secondary microcontroller, no OLED status module and no separate indicator bar.

## 2. Voice and assistant behaviour

The firmware keeps the XiaoZhi microphone, speaker, Wi-Fi and assistant pipeline. The on-device command matcher runs after final user speech-to-text is delivered to the face display. A recognized command is sent to a worker task so motor and servo actions do not run inside the LVGL display lock. For a recognized command, the code sets a four-second mute window and aborts an assistant speech response if the assistant enters Speaking during that window.

### Instant voice commands implemented in this source

| Say | Action |
|---|---|
| “Turn left” | Left pivot for about 600 ms |
| “Turn right” | Right pivot for about 600 ms |
| “Turn around” | Left pivot for about 1200 ms |
| “Go forward” / “move ahead” | Forward for 1500 ms |
| “Go backward” / “back up” / “reverse” | Backward for 1500 ms |
| “Stop” / “halt” / “freeze” / “hold on” | Stop immediately |
| “Follow me” / “follow” | Follow mode |
| “Wander” / “roam” / “explore” / “move around” / “go play” / “patrol” | Wander mode |
| “Power off” / “shut down” / “switch off” | Run soft power-off sequence |
| “Go to sleep” / “goodnight” / “take a nap” | Set the visual sleepy pose; this is not yet a true low-power listening sleep |
| “Wake up” | Run the wake pose sequence |
| “Ears up” / “perk up” | Raise both ears |
| “Ears down” | Lower both ears |
| “Wiggle your ears” | Opposite ear pose |
| “Dance” | Start the simple tongue-dance movement |
| “Spin” / “spin around” | Timed left pivot |
| “Yawn” | Droop ears and lower neck |
| “Nod yes” | Set the neck nod pose |
| “Shake no” | Set the neck side pose |

The matcher is intentionally small at this stage. It does not yet parse arbitrary “for N seconds” expressions or all of the phrases in the original design brief. A phrase that is not listed may still be understood by the cloud assistant, but it is not guaranteed to move the robot instantly.

### Paste this into the agent prompt on xiaozhi.me

> You are M'DEE, a small wheeled pet robot. When the user gives a movement or device command, call the matching tool IMMEDIATELY. Never ask whether they want to talk or what they want. After the tool runs, reply with at most four words, like 'Turning left.' or 'Okay.' Turn left or right = about 600 ms. Turn around = about 1200 ms. Forward or back with no time = 1500 ms. 'You can move around', 'go explore', 'roam', 'wander', 'free roam', 'go play' = set mode wander. 'Stop', 'stay', 'freeze', 'hold on' = stop. 'Follow me' = mode follow. Never describe your tools. Keep every answer short.

## 3. Movement and obstacle handling

### Movement commands

| Direction | Behaviour |
|---|---|
| Forward | Both motors forward |
| Backward | Both motors reverse |
| Left | Pivot left |
| Right | Pivot right |
| Stop | Both motor inputs low |

Speed presets are slow (25%), medium (50%), fast (75%) and turbo (100%) of the configured motor-voltage range. The range is 2.4–5.5 V. The controller converts the requested motor voltage to PWM duty using the measured pack voltage; if the battery reading is unavailable, it assumes 7.4 V. The configured 5.5 V ceiling is not exceeded by the firmware's duty calculation.

The control task ramps changes by approximately 8 percentage points every 20 ms. A stop command sets the target and current outputs to zero immediately. Phone joystick updates use a 450 ms deadman hold; if updates stop, the target returns to zero.

### Ultrasonic sensors

- The two HC-SR04 modules share one TRIG output.
- Each ECHO has its own 1k/2k divider.
- The code takes three readings and uses the median.
- 400 cm means no usable echo / nothing detected.
- The sensor task checks the front and rear sensors repeatedly.

The front and rear stop distance is 18 cm. The current code stops a forward command when the front distance is below this threshold and stops reverse when the rear distance is below it.

### Autonomy modes

| Mode | Current behaviour |
|---|---|
| `manual` | Only explicit commands move the robot. A manual drive command cancels autonomy. |
| `wander` | Moves forward at medium speed; if the front reading is below about 24 cm it reverses when the rear is clear, otherwise pivots. |
| `follow` | Moves forward when the front distance is 40–90 cm; backs up slowly if closer than 22 cm and the rear is clear; otherwise stops. |

The current wander implementation is a basic obstacle reaction, not the complete random-roaming algorithm in the design brief. Slowdown between 18 and 40 cm, random turn duration, stuck-count handling and advanced path selection still need work.

**There are no cliff or edge sensors. Never leave M'DEE roaming unattended near stairs, table edges, balconies, ramps or other drops.** Test with the wheels lifted and keep a hand near the master power switch.

## 4. Servos and head poses

Four positional servos are supported:

- Neck turn: pose 0 = left, 50 = centre, 100 = right.
- Neck tilt: pose 0 = chin down, 50 = level, 100 = looking up.
- Ears: pose 0 = droop/down, 100 = perk/up.

The servo angle conversion uses calibration constants in `main/boards/mdee-robot/config.h` and clamps the resulting angle to 15–165 degrees. This is important because the servo mechanical end stops have been removed.

Each servo can be disabled independently:

| Servo | Enable constant |
|---|---|
| Neck turn | `MDEE_SERVO_NECK_TURN_EN` |
| Neck tilt | `MDEE_SERVO_NECK_TILT_EN` |
| Left ear | `MDEE_SERVO_LEFT_EAR_EN` |
| Right ear | `MDEE_SERVO_RIGHT_EAR_EN` |

Set a constant to `0` if that servo is not fitted. The current code provides direct pose changes and a step-based wake movement. Per-servo time-based easing, independent ear routines, wiggle overlays and a public detach/reattach control are not yet complete.

## 5. Face display and status display

### Face screen

The 2.0-inch ST7789 face is driven by LVGL. It has two animated eyes, eyelids, gaze movement, a mouth/arc and a boot-logo GIF overlay. XiaoZhi emotion and status callbacks update the face.

The existing source maps a subset of emotions, including neutral, happy, laughing, loving, embarrassed, winking, cool, sad, crying, angry, surprised, thinking, confused and sleepy. The full requested list of EMO-style eye types, tears, hearts, sparkles, sweat, blush and all additional moods has not yet been implemented.

### Status screen

The 1.54-inch ST7789 status display uses a framebuffer in PSRAM and two alternating DMA strip buffers. It refreshes about twice per second and currently shows:

- M'DEE title
- Current state text
- Battery voltage and percentage
- Battery bar
- Front and rear distance
- Current mode
- Most recent chat line

The full planned Wi-Fi signal bars, three-line chat history, mood values, idle level, phone URL and alert system are not all implemented yet.

## 6. Lights

### Four WS2812 pixels

- Pixels 0–1 are the left bar.
- Pixels 2–3 are the right bar.
- Global brightness is controlled from 0–100%.

The source supports off, solid, breathing, rainbow, heartbeat-like pulse, flash, two-pixel chase and sparkle-style effects. The current implementation applies an effect to the four-pixel strip; independent per-bar colour/effect control is still future work.

### Two blue LEDs

The two blue LEDs share GPIO18 through a suitable driver stage. Supported modes are off, on, breathe, blink and pulse. Brightness is adjustable from 0–100%.

## 7. Battery and protection

The firmware reads the 2S pack through GPIO1 using ADC1, a 100k/22k divider, curve-fitting calibration when available and a 16-sample average. It converts pack voltage to per-cell voltage and estimates percentage using the piecewise cell-voltage curve in the project specification.

The motor control task has a software cutoff if measured pack voltage stays below 6.3 V for five seconds. The firmware's motor-voltage ceiling is 5.5 V. The motor cutoff is implemented below 6.3 V for five seconds. A critical reading below 6.2 V for ten seconds starts the soft power-off task. At 15% or below for five seconds, the source changes the face to sad, switches the bars to red breathing and blinks the blue LEDs. The low-battery spoken announcement and synthesized warning sound are not implemented yet; all thresholds still need bench validation.

A software voltage estimate is not a hardware protection device. Keep the BMS, fuse and master switch installed.

## 8. Soft power-off and wake

### Soft power-off

The current power-off task:
1. Stops the motors.
2. Changes the face to sleepy and status text to “Goodnight”.
3. Lowers the ears and neck.
4. Fades the light brightness.
5. Stops servo PWM so the servos go limp.
6. Turns off the status display and backlight.
7. Configures GPIO0 as a low-level wake source and enters deep sleep.

The current implementation does not yet send the ST7789 sleep-in command to both panels, disable the audio codec explicitly, or render the exact eye-close-to-line-to-dot animation. The hardware master switch still cuts power immediately and cannot play an animation.

### Wake-up

On boot, servo poses are initialized down/centred. After a short pause, a step-based sequence raises the ears and neck and turns on the lights. The complete planned yawn, overshoot wiggle and synthesized startup sound are not yet present.

### Nap

The current `sleep` action only applies a sleepy pose and dims the mood lighting. It does not yet enter ESP32 light sleep while maintaining wake-word listening. Treat it as a visual nap pose, not a true low-power sleep mode.

## 9. MCP tools currently registered

| Tool | Main arguments / effect |
|---|---|
| `self.robot.move` | `direction`, `speed`, `duration_ms` |
| `self.robot.stop` | Immediate stop |
| `self.robot.set_mode` | `manual`, `wander`, `follow` |
| `self.robot.neck` | `turn`, `tilt` pose values 0–100 |
| `self.robot.ears` | `gesture`: up, down, wiggle, flick, neutral, auto |
| `self.robot.perform` | Named short movement/pose routine |
| `self.robot.set_emotion` | Emotion name |
| `self.robot.power` | `sleep`, `power_off`, `wake` |
| `self.robot.get_status` | Battery, distances, mode and wheel state |
| `self.lights.bars` | Effect and RGB colour |
| `self.lights.blue` | Blue LED mode |
| `self.lights.brightness` | `bars`, `blue`, `screens` or `all` |
| `self.system.memory` | Free internal heap, free PSRAM and largest internal free block |
| `self.system.phone_api` | Phone page URL and endpoint list |
| `self.system.status` | Firmware version, uptime, Wi-Fi RSSI/IP and robot status |

The tools are described to the assistant as immediate actions with short replies. Temperature reporting and the remaining planned system tools are not registered yet.

## 10. Phone web page

Connect the phone and robot to the same Wi-Fi network. Find the ESP32-S3's IP address in the XiaoZhi network information or router device list, then open:

```text
http://ROBOT_IP/
```

The page currently includes a joystick, speed presets, STOP, manual/wander/follow mode, neck and ear controls, light controls, brightness controls, performance buttons and power buttons. The joystick sends drive updates while held and sends STOP when released. The web server uses port 80.

### HTTP API endpoints

| Endpoint | Purpose |
|---|---|
| `/api/status` | Current status JSON |
| `/api/drive?l=...&r=...&h=450` | Joystick wheel percentages and deadman hold |
| `/api/move?dir=forward&speed=medium&ms=1500` | Timed move |
| `/api/stop` | Stop |
| `/api/mode?m=manual` | Set mode |
| `/api/head?pan=50&tilt=50` | Neck poses |
| `/api/ears?g=wiggle` | Ear gesture |
| `/api/perform?n=greet` | Performance routine |
| `/api/emotion?n=happy` | Emotion |
| `/api/lights?p=chase` | Legacy light pattern |
| `/api/bars?e=rainbow` or `/api/neo?e=rainbow` | NeoPixel effect |
| `/api/blue?m=breathe` | Blue LEDs |
| `/api/brightness?t=bars&p=65` | Brightness |
| `/api/power?a=sleep` | Power/nap action |

Optional PIN protection is controlled by `WEB_CONTROL_PIN` in `config.h`. Leave it empty for an open page on a trusted local Wi-Fi network, or set a PIN and add it as the `k` query parameter.

## 11. BOOT button

| Action | Behaviour |
|---|---|
| Single click | Toggle XiaoZhi chat/listening state |
| Double click | Emergency motor stop |
| Long press (2 seconds) | Start soft power-off |
| Deep-sleep wake | Press BOOT to pull GPIO0 low |

## 12. Main firmware settings

| File | Setting | Purpose |
|---|---|---|
| `main/boards/mdee-robot/config.h` | `MOTOR_MAX_VOLTS` | Maximum software motor voltage |
| `main/boards/mdee-robot/config.h` | `MOTOR_MIN_START_VOLTS` | Minimum target voltage at nonzero speed |
| `main/boards/mdee-robot/config.h` | `MOTOR_*_PCT` | Speed preset levels |
| `main/boards/mdee-robot/config.h` | `ULTRASONIC_*` | Sensor pins and stop distance |
| `main/boards/mdee-robot/config.h` | `SERVO_*_0`, `SERVO_*_100` | Servo calibration |
| `main/boards/mdee-robot/config.h` | `MDEE_SERVO_*_EN` | Enable/disable each servo |
| `main/boards/mdee-robot/config.h` | `BATTERY_R_UPPER_OHM`, `BATTERY_R_LOWER_OHM` | Battery divider |
| `main/boards/mdee-robot/config.h` | `BATTERY_CAL_FACTOR` | ADC calibration adjustment |
| `main/boards/mdee-robot/config.h` | `WEB_CONTROL_PIN` | Optional phone-page PIN |
| `main/boards/mdee-robot/config.json` | `sdkconfig_append` | 16 MB flash, octal PSRAM, partitions, HTTP sockets and LVGL GIF |
| `.github/workflows/mdee.yml` | IDF image and build command | Cloud compilation |

## 13. Known unfinished features

The original design brief includes more than the current source implements. The following should not be assumed to work yet:

- Complete EMO-style eye modes and all requested emotion names.
- Full 2.5-second animated face shutdown and exact wake animation.
- True light-sleep nap while wake-word listening remains active.
- Synthesized sound library and nonblocking sound queue.
- Mood/energy/happiness/curiosity/sleepiness engine and time-of-day greetings.
- Idle-level engine and scheduled idle actions.
- Full list of dance/performances, cancellation handling and motion choreography.
- Randomized wander behaviour, slowdown zone and stuck detection.
- Low-battery announcement and critical-battery shutdown sequence.
- Independent left/right bar colour/effect control.
- Complete MCP system status, temperature and phone API tools.
- Full phone API for every feature in the original specification.

## 14. Build and update from Termux

The project is designed for phone-first GitHub Actions builds. Follow [TERMUX_GUIDE.md](TERMUX_GUIDE.md).

The workflow builds one merged image using ESP-IDF 6.1. The artifact is `mdee_<commit-sha>` and contains `merged-binary.bin`, flashed at address `0x0`. A successful GitHub Actions build is still required before flashing; this source package has not been compiled locally in this workspace.
