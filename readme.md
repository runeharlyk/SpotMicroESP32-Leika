<h1 align="center">
  <br />
  <a href="https://github.com/runeharlyk/SpotMicroESP32-Leika">
    <img src="https://raw.githubusercontent.com/runeharlyk/SpotMicro-Leika/main/assets/logo.jpg" alt="Leika" width="350">
  </a>
  <br>  
  Spot Micro - Leika
</h1>

<h4 align="center">A small quadruped robot, inspired by boston dynamic <a href="https://bostondynamics.com/products/spot/" target="_blank">Spot</a>.</h4>

<p align="center">
  <a href="#key-features">Key Features</a> •
  <a href="#overview">Overview</a> •
  <a href="#getting-started">Documentation</a> •
  <a href="#credits">Credits</a> •
  <a href="#license">License</a>
</p>

<!-- GIF
![screenshot](https://raw.githubusercontent.com/runeharlyk/SpotMicro-Leika/main/assets/logo.jpg) -->

## Status

<div style="display:flex;gap:20px">

[![Frontend Tests](https://github.com/runeharlyk/SpotMicroESP32-Leika/actions/workflows/frontend-tests.yml/badge.svg)](https://github.com/runeharlyk/SpotMicroESP32-Leika/actions/workflows/frontend-tests.yml)
[![PlatformIO CI](https://github.com/runeharlyk/SpotMicroESP32-Leika/actions/workflows/embedded-build.yml/badge.svg)](https://github.com/runeharlyk/SpotMicroESP32-Leika/actions/workflows/embedded-build.yml)

</div>

## Key Features

* Live preview - Make changes, See changes
* Real time data
  * Camera live stream, sensors, analytics, etc
* Full kinematic model
* Dual joystick controller
* Robot mirroring visualization
* Highly customizable
* Self hosted, self included
<!-- * Servo calibration tool -->

## Overview

This repository contains the source code for a Spot Micro quadruped robot.
Execution of the software takes place on a ESP32 cam, which runs various number of FreeRTos task for seamless robotic operations.
By focusing on practicality and simplicity in both hardware and software, it offer an accessible platform for learning, experimentation, and modest real-world applications.

### Documentation

You can find the current steps to get a fresh new doggo up and barking on [/docs](docs/readme.md)

1. [Components](docs/1_components.md)
1. [Assembly](docs/2_assembly.md)
1. [Software](docs/3_software.md)
1. [Turning on for the first time](docs/4_configuring.md)
1. [Running](docs/5_running.md)
1. [Developing](docs/6_developing.md)
1. [Contributing](docs/7_contributing.md)

#### Software
You can find a description for the current esp32 firmware and controller [here](docs/software_description.md).

<img src="images/controller.gif" alt="controller" width="500">

## Kinematics

The kinematic for the robot is from this [kinematics paper](https://www.researchgate.net/publication/320307716_Inverse_Kinematic_Analysis_Of_A_Quadruped_Robot). A C++ and TypeScript library was written to enable onboard calculation and fast development iteration using the robot mirroring. 

## Getting started

1. Clone and open the new project

    ```sh
    git clone https://github.com/runeharlyk/SpotMicroESP32-Leika
    ```

1. Install dependencies with preferable package manager (npm, pnpm, yarn)

    ```sh
    cd app
    pnpm install
    ```

1. Configure device settings
    1. Update `factory_settings.ini` with relevant settings

1. Upload filesystem image using platformIO

1. Upload firmware using platformIO

## Future

See the [project backlog](https://github.com/users/runeharlyk/projects/3) and [open issues](https://github.com/runeharlyk/SpotMicroESP32-Leika/issues) for full list of proposed and active features (and known issues).

## Credits

This project takes great inspiration from the following resources:

1. [Spot Micro Quadruped Project - mike4192](https://github.com/mike4192/spotMicro)
1. [Kinematics](https://www.researchgate.net/publication/320307716_Inverse_Kinematic_Analysis_Of_A_Quadruped_Robot)
1. [ESP32SvelteKit template](https://github.com/theelims/ESP32-sveltekit)
1. [SpotMicro ESP32 - Maarten Weyn](https://github.com/maartenweyn/SpotMicro_ESP32)
1. [SpotMicroAi](https://gitlab.com/public-open-source/spotmicroai)
1. [Spot Micro - Leika](https://github.com/runeharlyk/SpotMicro-Leika/tree/main)
1. [NightDriverStrip](https://github.com/PlummersSoftwareLLC/NightDriverStrip)

## Support

If you like the project and want to follow it evolving concidering ✨-ing the project

<a href="https://bmc.link/runeharlyk" target="_blank"><img src="https://www.buymeacoffee.com/assets/img/custom_images/purple_img.png" alt="Buy Me A Coffee" style="height: 41px !important;width: 174px !important;box-shadow: 0px 3px 2px 0px rgba(190, 190, 190, 0.5) !important;-webkit-box-shadow: 0px 3px 2px 0px rgba(190, 190, 190, 0.5) !important;" ></a>

## You may also like...

* [Spot Micro Quadruped Project - mike4192](https://github.com/mike4192/spotMicro) - Great ROS based project
* [SpotMicroAi](https://gitlab.com/public-open-source/spotmicroai) - Group repository with simulations and runtimes

## License

[MIT](LICENSE.md)

---

> [runeharlyk.dk](https://runeharlyk.dk) &nbsp;&middot;&nbsp;
> GitHub [@runeharlyk](https://github.com/runeharlyk) &nbsp;&middot;&nbsp;
> LinkedIn [@Rune Harlyk](https://www.linkedin.com/in/rune-harlyk/)
