# M'DEE wiring and power guide

Target: one ESP32-S3 N16R8 WROOM-1 board, 16 MB flash and 8 MB octal PSRAM. All GPIO signals are 3.3 V logic unless the module itself is powered from 5 V.

> **Safety first:** two controller boards were previously damaged during power experiments. A lithium-ion pack can overheat or catch fire if it is shorted, damaged or charged incorrectly. Use a professionally assembled, protected pack and the correct charger; have an experienced adult or qualified electronics technician handle battery/BMS/fuse connections and live power-rail adjustments. Do not assemble or solder directly to loose cells. Do not connect the ESP32-S3 until both LM2596 modules have been adjusted and measured. Never apply 5 V to an ESP32 GPIO. If you are unsure about a connection, leave it disconnected and verify it with a multimeter first.

## 1. Main parts

- ESP32-S3 N16R8 WROOM-1 controller board
- INMP441 I2S microphone
- MAX98357A I2S amplifier and 3 W speaker
- 2.0-inch ST7789V 240×320 TFT (face)
- 1.54-inch ST7789 240×240 TFT (status)
- TB6612FNG dual H-bridge and two 2.5–6 V stunt-car motors
- Two HC-SR04 ultrasonic modules
- Four WS2812 addressable LEDs
- Two blue LEDs with current-limiting resistors/transistor driver
- Four positional SG90-type servos (each can be disabled in `config.h`)
- 2S2P Li-ion pack, 2S BMS, 5 A fuse, master switch
- Two LM2596 buck converters, capacitors and divider resistors

## 2. ESP32-S3 pin map

| Function | ESP32-S3 GPIO | Connection |
|---|---:|---|
| I2S BCLK | 4 | INMP441 SCK and MAX98357A BCLK |
| I2S WS/LRCLK | 5 | INMP441 WS and MAX98357A LRC |
| Microphone data | 6 | INMP441 SD → ESP32 DIN |
| Amplifier data | 7 | ESP32 DOUT → MAX98357A DIN |
| SPI MOSI | 11 | Both TFT MOSI |
| SPI SCLK | 12 | Both TFT SCLK |
| Shared display DC | 9 | Both TFT D/C |
| Shared display RST | 8 | Both TFT RESET |
| Face display CS | 10 | 2.0-inch TFT CS |
| Status display CS | 13 | 1.54-inch TFT CS |
| Shared backlight PWM | 42 | Both TFT BL controls (use a transistor if required by BL current) |
| TB6612 AIN1 | 38 | Left motor direction/PWM input 1 |
| TB6612 AIN2 | 39 | Left motor direction/PWM input 2 |
| TB6612 BIN1 | 40 | Right motor direction/PWM input 1 |
| TB6612 BIN2 | 41 | Right motor direction/PWM input 2 |
| HC-SR04 TRIG | 2 | Both TRIG pins joined |
| Front HC-SR04 ECHO | 3 | Through its own 1k/2k divider |
| Rear HC-SR04 ECHO | 46 | Through its own 1k/2k divider |
| WS2812 DIN | 48 | First pixel DIN through 330 Ω |
| Blue LEDs control | 18 | Transistor/resistor driver input |
| Neck turn servo | 14 | Signal |
| Neck tilt servo | 15 | Signal |
| Left ear servo | 16 | Signal |
| Right ear servo | 17 | Signal |
| Battery divider | 1 | ADC1_CH0 |
| BOOT button | 0 | On-board BOOT button |
| Reserved, unused | 21, 47 | Do not connect |

GPIO3 is a strapping pin. It is used only as the front sensor input; keep the ECHO divider connected as shown and avoid forcing this pin to an unusual level during reset. GPIO46 is input-only and is used only for the rear ECHO input. GPIO48 may also be connected to an on-board RGB LED on some ESP32-S3 boards; that LED may mirror the first NeoPixel.

## 3. Audio connections

| Module pin | Connect to |
|---|---|
| INMP441 VDD | 3.3 V |
| INMP441 GND | GND |
| INMP441 SCK | GPIO4 |
| INMP441 WS | GPIO5 |
| INMP441 SD | GPIO6 |
| INMP441 L/R | GND (left channel) |
| MAX98357A VIN | LM2596 #1 regulated 5.0 V |
| MAX98357A GND | Common GND |
| MAX98357A BCLK | GPIO4 |
| MAX98357A LRC | GPIO5 |
| MAX98357A DIN | GPIO7 |
| Speaker | MAX98357A speaker outputs |

Do not connect either speaker output to ground. The amplifier uses a bridged output.

## 4. Displays

Both displays share MOSI, SCLK, D/C and RESET. They must have separate chip-select wires: GPIO10 for the face and GPIO13 for status. Connect each module's power according to its actual board specification; do not assume every ST7789 breakout accepts 5 V.

The firmware initializes the face panel first with the shared hardware reset. It then initializes the status panel using software reset so it does not reset the face panel. Both backlight control pins join GPIO42 only if the combined backlight current is handled by a suitable transistor/driver stage.

## 5. TB6612FNG and motors

| TB6612 pin | Connection |
|---|---|
| VM | Fused 2S battery positive (6.0–8.4 V) |
| VCC | ESP32 3.3 V logic rail |
| GND | Common ground |
| AIN1 / AIN2 | GPIO38 / GPIO39 |
| BIN1 / BIN2 | GPIO40 / GPIO41 |
| PWMA / PWMB | Tie high to 3.3 V |
| STBY | Tie high to 3.3 V |
| AO1 / AO2 | Left motor terminals |
| BO1 / BO2 | Right motor terminals |

Place approximately 470 µF across VM and GND near the motor driver. Add 100 nF suppression capacitors across each motor's terminals. Keep motor wiring short and away from the microphone and display signal wiring.

**Important motor-voltage note:** the battery can reach 8.4 V while the motors are rated only up to 6 V. The firmware scales the PWM duty so the commanded average motor voltage does not exceed the configured 5.5 V ceiling. This is not a substitute for checking motor current, driver temperature, stall current and actual motor-terminal voltage under load. If the motor/driver combination is unsuitable for the pack, use a suitable hardware regulator or a driver with an appropriate voltage rating.

## 6. Ultrasonic ECHO dividers

HC-SR04 ECHO is typically 5 V and must not connect directly to an ESP32 GPIO. Build one divider per sensor:

```text
HC-SR04 ECHO ---- 1 kΩ ----+---- ESP32 ECHO GPIO
                           |
                          2 kΩ
                           |
                          GND
```

Use the divider output node for GPIO3 (front) and GPIO46 (rear). Join both TRIG pins to GPIO2. Power the HC-SR04 modules from the regulated 5 V rail and connect their grounds to common ground.

## 7. NeoPixels and blue LEDs

- Chain four WS2812 pixels: pixel 0 → pixel 1 → pixel 2 → pixel 3.
- GPIO48 goes through a 330 Ω series resistor to pixel 0 DIN.
- Pixel 0–1 form the left bar; pixel 2–3 form the right bar.
- Power the pixels from the regulated 5 V rail and connect GND to common ground.
- Add a bulk capacitor near the LED power input.
- Two blue LEDs are controlled together from GPIO18 through a transistor driver and suitable current-limiting resistors. Do not draw LED current directly from the GPIO pin.

A 3.3 V data signal may not meet the input-high threshold of every 5 V WS2812 variant. If the pixels behave unpredictably, add a 5 V AHCT-family level shifter between GPIO48 and the first pixel.

## 8. Four positional servos

| Servo | Signal GPIO | Separate 5 V supply |
|---|---:|---|
| Neck turn | 14 | LM2596 #2 |
| Neck tilt | 15 | LM2596 #2 |
| Left ear | 16 | LM2596 #2 |
| Right ear | 17 | LM2596 #2 |

Connect all servo grounds to the common ground. The servo power positive wires go to LM2596 #2, not to the ESP32 3.3 V pin. Use a supply with enough current for simultaneous servo movement and add approximately 1000 µF near the servo distribution point.

### Servo calibration — no mechanical end stops

The servo stops have been removed, so do not sweep to 0° or 180° during testing. Firmware clamps its configured mechanical angle range to 15–165° and maps the 0–100 pose scale through calibration values in `config.h`.

1. Disconnect the servo horn/linkage from the mechanism.
2. Power the servo rail from a current-limited 5 V supply.
3. Start with the pose midpoint and verify the shaft direction.
4. Adjust the `SERVO_*_0` and `SERVO_*_100` values gradually.
5. Refit the horn only when the servo is centered and the mechanism is not under load.
6. If a servo is absent or unsafe, set its corresponding `MDEE_SERVO_*_EN` value to `0`.

The default mapping is provisional and must be calibrated on the actual robot. Never force a servo against a physical stop.

## 9. Battery measurement divider

Use a 100 kΩ upper resistor and 22 kΩ lower resistor:

```text
2S pack positive ---- 100 kΩ ----+---- GPIO1 (ADC1_CH0)
                                 |
                                22 kΩ
                                 |
                                GND
                                 |
                               100 nF
                                 |
                                GND
```

The 100 nF capacitor is connected from the ADC node to GND. At 8.4 V pack voltage, the ADC node is approximately 1.52 V. Keep the battery divider ground connected to the ESP32 ground. `BATTERY_CAL_FACTOR` in `config.h` may be adjusted after comparing the firmware reading with a multimeter.

## 10. Power distribution

Use the 2S2P pack, 2S BMS, 5 A fuse and master switch. The BMS and fuse must be suitable for the actual pack and motor current. Use protected, matched cells and a proper 2S charger.

```text
2S2P battery pack
       |
      2S BMS
       |
     5 A fuse
       |
   Master switch
       |
       +-------------------- TB6612 VM ---- motors
       |
       +---- LM2596 #1 set to 5.00 V
       |          +---- ESP32-S3 5V/VIN input
       |          +---- MAX98357A VIN
       |          +---- HC-SR04 modules
       |          +---- WS2812 5V
       |          +---- blue LED driver supply
       |
       +---- LM2596 #2 set to 5.00 V
                  +---- four servo V+ wires

All grounds (BMS output, both LM2596 modules, ESP32,
TB6612, audio, sensors, displays, LEDs and servos) join.
```

Add approximately 1000 µF across the 5 V output of each LM2596 near its loads. The motor driver also needs its local VM capacitor. Keep the servo 5 V rail separate from the ESP32/audio/sensor 5 V rail to reduce brownouts. The two regulated rails share ground but their positive outputs must not be joined.

The ESP32-S3 board must be powered through its documented 5 V/VIN input, not through a GPIO. Do not connect USB while external 5 V is attached unless the board's USB/external-power isolation is known to be safe.

## 11. First power-up checklist

- [ ] Remove the ESP32-S3 from the circuit.
- [ ] Inspect polarity, solder bridges and exposed wires.
- [ ] Check the BMS output and fuse wiring with a multimeter.
- [ ] Power LM2596 #1 with no load and adjust it to exactly 5.00 V.
- [ ] Power LM2596 #2 with no load and adjust it to exactly 5.00 V.
- [ ] Verify both rails again after switching the master switch off and on.
- [ ] Check for accidental continuity between 5 V and GND.
- [ ] Test the ESP32-S3 on USB alone with no external wires attached.
- [ ] Disconnect USB, then connect only the regulated 5 V logic rail and common ground.
- [ ] Add the displays, microphone and amplifier one module at a time.
- [ ] Add the sensors and LED strip, checking signal voltage before connecting each GPIO.
- [ ] Add the motor driver with motors lifted clear of the floor.
- [ ] Add the servo supply last; verify its 5 V rail does not sag.
- [ ] Test STOP and emergency stop before allowing the wheels to touch the floor.

Never feed 5 V into a GPIO. Do not use the ESP32 board's 3.3 V regulator to power servos, motors or the speaker amplifier.

## 12. Battery and runtime estimate

Four 3.7 V, 1200 mAh cells in a 2S2P arrangement give approximately 7.4 V nominal and 2.4 Ah:

- Nominal energy: approximately 17.8 Wh.
- Full-charge pack voltage: 8.4 V.
- Estimated runtime: approximately 1.5–2 hours under moderate mixed use.

This is only an estimate. Actual runtime depends on motor load, floor friction, servo activity, audio volume, Wi-Fi use, battery age and converter efficiency. Measure the real current draw and temperature during supervised tests.

## 13. Troubleshooting

| Symptom | Checks |
|---|---|
| ESP32 resets when wheels move | Separate motor power from logic power; check common ground, bulk capacitors and BMS current rating. |
| ESP32 resets when ears/neck move | Check LM2596 #2 current capacity, servo wiring and common ground. |
| Ultrasonic reading is always 400 cm | Check TRIG, ECHO polarity, divider node, ground and sensor power. |
| ESP32 gets hot or does not boot | Disconnect all modules; test USB-only, then reconnect one module at a time. |
| TFT stays white/black | Check CS, shared DC/RST, SPI wiring, panel power and orientation settings. |
| TFT backlight stays dark | Check GPIO42 PWM and whether the backlight requires a transistor driver. |
| WS2812 pixels flicker | Check 5 V supply, ground, 330 Ω series resistor and data-level shifting. |
| Servo jitters | Check separate servo rail, ground, calibration and mechanical binding. |
| Battery reading is wrong | Measure the pack and ADC divider node; adjust `BATTERY_CAL_FACTOR`. |
| Motors still feel too fast | Lift wheels, check PWM polarity and verify motor terminal voltage and current with suitable instruments. |
