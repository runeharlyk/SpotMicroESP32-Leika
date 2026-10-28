# Welcome to the docs

Here you will find the current documentation for the ESP32 based spot micro project, Leika.

## Guide

These are the steps it takes to get a fresh new robot up and barking.

1. [Components](1_components.md)
1. [Assembly](2_assembly.md)
1. [Software](3_software.md)
1. [Turning on for the first time](4_configuring.md)
1. [Running](5_running.md)
1. [Developing](6_developing.md)
1. [Contributing](7_contributing.md)

## About Spot

- [Robots capabilities](spot.md)
- [Software description](software_description.md) (firmware tasks, sensors, feature flags and the web app)
- [Kinematics](kinematics.md) (body pose, inverse kinematics and the hardware variants)
- [Conventions](conventions.md) (the body frame, the leg numbering and the signs of the sticks, the body pose, the odometry and the goals)
- [Motion system](motion_system.md) (modes, controller mapping and the trot and crawl gaits)
- [Floor benchmark](benchmark.md) (scoring the odometry and goal moves against an overhead camera)
- [Animations](animation.md) (playing, writing and uploading clips, and the clip format)
- [API](api.md)
- [WebSocket](websocket.md) (the protobuf messages, subscriptions, the dead-man stop and telemetry)
- [Serial link](serial.md) (the same requests over a USB cable, beside the log)
- [Connectivity](connectivity.md) (how the app reaches the robot from each origin, and why)
- [Bluetooth link](bluetooth.md) (driving and settings over Bluetooth LE, on boards with PSRAM)
- [Hardware acceptance](hardware-acceptance.md) (what has run on the real robot, and the checks still waiting for it)
