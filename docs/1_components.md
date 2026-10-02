# Components

Spot is comprised of a 3D-printed body, some hardware, and a list of electronic components.

## Hardware

Spot is 3D-printed and is a combination of different Spot Micro designs, with some minor modifications.
The original design was developed by KDY0523.

- [robjk reinforced shoulder remix](https://www.thingiverse.com/thing:4937631)
- [Kooba SpotMicroESP32 remix](https://www.thingiverse.com/thing:4559827)
- [KDY0532 original design](https://www.thingiverse.com/thing:3445283)

The 3D prints are assembled with some additional non-printable components:

- 84x M2x8 screws + M2 nuts
- 92x M3x8 screws + M3 nuts
- 64x M3x20 screws + M3 nuts
- 12x 625ZZ ball bearings

## Electronics

These are the electronics I used for mine, and they can easily be swapped to suit your Spot's needs.

| Component                 | Specification                 | Required | Recommendation                                                                                          |
| ------------------------- | ----------------------------- | -------- | ------------------------------------------------------------------------------------------------------- |
| ESP32                     | Brain                         | Yes      | ESP32-S3 (N8R8) with a camera.                                                                          |
| OV2640 or OV5640          | Camera                        | No       | 120-160 degrees. The ESP32-P4 build uses an OV5647 on its MIPI CSI port                           |
| PCA9685                   | Servo board                   | Yes      | Add thicker solder traces                                                                               |
| 12x Servo motors          | Actuators                     | Yes      | 20kg-35kg with high speed. If they are rated for your battery voltage you can skip the step down module |
| IMU                       | ICM-20948, BNO055 or MPU6050  | No       | Select one in `esp32/features.ini`. The BNO055 and ICM-20948 include a compass                          |
| HMC5883                   | Magnetometer                  | No       | A separate compass for an IMU without one. The GY-87 includes it                                        |
| PAJ7620U2                 | Gesture sensor                | No       | For interaction capabilities                                                                            |
| Power switch              | Main power switch             | Yes      |                                                                                                         |
| Power button w/ led       | Mode switch controller        | No       | The firmware does not read a button. Modes are switched from the web app                                |
| 2x HC-SR04                | Ultrasonic Distance Sensor    | No       | Not usable yet: `USE_USS` includes `NewPing.h`, which the build does not provide. Each sensor would use one GPIO (`USS_LEFT_PIN`, `USS_RIGHT_PIN`), which only the esp32-wroom-camera environment defines |
| BMP180                    | Barometer                     | No       | Enable `USE_BMP180` in `esp32/features.ini`. Reports pressure, temperature and altitude over I2C        |
| WS2812 strip              | Status LEDs                   | No       | Enable `USE_WS2812` in `esp32/features.ini`. The firmware drives 13 LEDs on `WS2812_PIN`                |
| LM2596 or XL4015          | DC-DC Stepdown Module         | Yes      | Should be set a 5V for the ESP32 and peripherals                                                        |
| 0.96" SD1306              | OLED display                  | No       | The firmware has no display driver                                                                      |
| SZBK07                    | 20A DC-DC Buck Converter      | No       | Stepdown to servo voltage. If you select servos rated for you battery voltage, you don't need this.     |
| 7.6-8.4V Battery          | Battery                       | No       | Im using 4x 18650 in 2s2p configuration, but other people have 2s LiPos.                                |
| 4x Servo extension cables | Servo extension cables        | Yes      | You can either buy them or make them with a couple or headers and some cable.                           |

On the standard Leika, the MG996R is the minimum servo, and the CLS6336HV is highly recommended.
The Leika Mini (the Pico) uses MG92B servos.

I recommend getting an ESP32-S3 with a camera, allowing for more computation and imaging capabilities.

It means a more responsive robot as it's faster at doing sensor fusion, calculating kinematics and gait planning, and networking.

## Boards

The PlatformIO environments in `platformio.ini` define which board and I2C pins the firmware is built for.

| Environment         | Board                                    | Camera                    | I2C (SDA, SCL) |
| ------------------- | ---------------------------------------- | ------------------------- | -------------- |
| `esp32-camera`      | ESP32-CAM (AI Thinker)                   | DVP                       | 14, 15         |
| `esp32-wroom-camera`| ESP32-S3-DevKitC-1, 8 MB flash and PSRAM | DVP, ESP32-S3-EYE pinout  | 47, 21         |
| `seeed-xiao-esp32s3`| Seeed XIAO ESP32S3                       | DVP                       | 5, 6           |
| `esp32dev`          | ESP32 dev board                          | None                      | 21, 22         |
| `esp32-p4`          | ESP32-P4                                 | MIPI CSI (OV5647)         | 7, 8           |
