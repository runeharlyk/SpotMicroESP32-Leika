# Software description

The software make use of a range of different libraries to enhance the functionality.
Up to date list can be seen in platformio.ini file.
The libraries includes:

- Esp32SvelteKit
- PsychicHttp
- ArduinoJson
- Adafruit SSD1306
- Adafruit GFX Library
- Adafruit BusIO
- Adafruit PWM Servo Driver Library
- Adafruit ADS1X15
- Adafruit HMC5883 Unified
- Adafruit Unified Sensor
- I2Cdevlib-MPU6050
- NewPing
- SPI

#### Structure

The software utilizes a couple of FreeRTos task
| Task | Description | Priority | Core
| --- | --- | --- | ---
| Idle0 task | Burns cpu time to track cpu usage for core 0 | 0 | 0
| Idle1 task | Burns cpu time to track cpu usage for core 1 | 0 | 1
| Spot service task | Handle synchronous services | 2 | 0
| Camera streaming tasks | Handle streaming to a single client | 1 | -1

#### Feature flags

To dis-/enable the major feature defines are used. Define them in either features.ini or in platformio.ini's build_flags.
| Feature | Description | Default
| --- | --- | ---
| FT_SLEEP | Whether or not include sleep management | 0
| FT_UPLOAD_FIRMWARE | Whether or not to use OAT | 0
| FT_DOWNLOAD_FIRMWARE | Whether or not to use github for firmware updates | 0
| FT_ANALYTICS | Whether or not to use analytics service | 1
| FT_MOTION | Whether or not to use motion service | 1
| FT_IMU | Whether or not to use imu (MPU6050) | 1
| FT_MAG | Whether or not to use magnetometer (HMC5883) | 1
| FT_BMP | Whether or not to use barometer (BMP085) | 1
| FT_SERVO | Whether or not to use servo (PCA9685) | 1

### 📲 Controller

The controller is a SvelteKit app, which main focus is to calibrate and control the robot.

<!-- Write about the emulation, stream, controls and link to the space issues -->

It is made to be included and hosted by the robot.
Therefore there is placed a lot of thought behind the functionality and dependencies.

#### Development dependencies

For the development dependencies I choose the following

| Dependencies | Description                                                                                                                                                                                                                                                                                                                                  |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SvelteKit    | SvelteKit is an application framework built on top of Svelte, enhancing it with features like routing, server-side rendering, and static site generation. It streamlines the development process by integrating server-side capabilities with Svelte's client-side benefits. Furthermore it make the development process fast and enjoyable. |
| Vite         | Vite is a frontend tool that is used for building fast and optimized web applications. Is serves code local during development and bundles assets for production                                                                                                                                                                             |
| Typescript   | TypeScript's integration of static typing enhances code reliability and maintainability.                                                                                                                                                                                                                                                     |
| Tailwind CSS | Tailwind CSS accelerates web development with its utility-first approach, ensuring rapid styling and consistent design.                                                                                                                                                                                                                      |

#### Libraries

For the app functionality I choose the following:

| Dependencies                                               | Description                                                                                          |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| [Three](https://www.npmjs.com/package/three)               | Easy to use, lightweight, cross-browser, general purpose 3D library.                                 |
| [Urdf-loader](https://www.npmjs.com/package/urdf-loader)   | Utilities for loading URDF files into THREE.js and a Web Component that loads and renders the model. |
| [Xacro-parser](https://www.npmjs.com/package/xacro-parser) | Javascript parser and loader for processing the ROS Xacro file format.                               |
| [NippleJS](https://www.npmjs.com/package/nipplejs)         | A vanilla virtual joystick for touch capable interfaces.                                             |
| [Uzip](https://www.npmjs.com/package/uzip)                 | Simple, tiny and fast ZIP library.                                                                   |
| [ChartJS](https://www.npmjs.com/package/chart.js)          | Simple and flexible charting library.                                                                |
