# API

https://dev.bostondynamics.com/docs/concepts/choreography/choreography_in_tablet.html

| HTTP Method | Endpoint       | Description                | Parameters                |
|-------------|----------------|----------------------------|---------------------------|
| GET         | /api/sensor/battery       | Retrieve the battery state |                       |
| GET         | /api/sensor/mpu       | Retrieve the mpu state |                       |
| GET         | /api/sensor/magnetometer       | Retrieve the magnetometer state |                       |
| GET         | /api/sensor/distances       | Retrieve the distances state |                      |
| GET         | /api/sensor/distance/{position}       | Retrieve the distance state |    `position`: The position of the distance sensor **LEFT** and **RIGHT**                    |
| GET         | /api/sensor/stream    | Retrieve the camera stream  | |
| GET         | /api/actuator    | Retrieve the actuator states  | |
| GET         | /api/actuator/{id}    | Retrieve the actuator state for `id`  | `id`: The ID of the actuator |
| POST        | /api/actuator/{id}    | Set the actuator state  | `id`: The ID of the actuator|
| GET         | /api/kinematics/feet    | Retrieve the current feet positions as (x, y, z) coordinates|                           |
| GET         | /api/kinematics/body    | Retrieve the current body position as a (x, y, z) coordinates|                           |
| GET         | /api/kinematics/bodystate    | Retrieve the current body and feet positions |                           |
| GET         | /api/system/log    | Retrieve the system log  | |
| GET         | /api/system/info    | Retrieve the system information  | |
| GET         | /api/system/settings    | Retrieve the system settings  | |
| POST        | /api/system/settings    | Set the system settings  | |
| POST        | /api/system/reset    | Reset system  | |
| POST        | /api/system/power/off    | Power of the system  | |
| POST        | /api/system/stop    | Stop power to actuators  | `id`: The stop level **CUT**, **SETTLE_THEN_CUT**, **NONE** |
