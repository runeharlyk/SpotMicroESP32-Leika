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
| OV2640 or OV5640          | Camera                        | No       | 120-160°                                                                                                |
| PCA9685                   | Servo board                   | Yes      | Add thicker solder traces                                                                               |
| 12x Servo motors          | Actuators                     | Yes      | 20kg-35kg with high speed. If they are rated for your battery voltage you can skip the step down module |
| MPU6050                   | Inertial measuring unit (IMU) | No       | GY-87 or MPU-9250 include magnetometer                                                                  |
| HMC5883                   | Magnetometer                  | No       | GY-87 or MPU-9250 include magnetometer                                                                  |
| Power switch              | Main power switch             | Yes      |                                                                                                         |
| Power button w/ led       | Mode switch controller        | No       |                                                                                                         |
| 2x HC-SR04                | Ultrasonic Distance Sensor    | No       |                                                                                                         |
| LM2596 or XL4015          | DC-DC Stepdown Module         | Yes      | Should be set a 5V for the ESP32 and peripherals                                                        |
| 0.96" SD1306              | OLED display                  | No       |                                                                                                         |
| SZBK07                    | 20A DC-DC Buck Converter      | No       | Stepdown to servo voltage. If you select servos rated for you battery voltage, you don't need this.     |
| 7.6-8.4V Battery          | Battery                       | No       | Im using 4x 18650 in 2s2p configuration, but other people have 2s LiPos.                                |
| 4x Servo extension cables | Servo extension cables        | Yes      | You can either buy them or make them with a couple or headers and some cable.                           |

I recommend getting an ESP32-S3 with a camera, allowing for more computation and imaging capabilities.

It means a more responsive robot as it's faster at doing sensor fusion, calculating kinematics and gait planning, and networking.
