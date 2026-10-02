# Running the spot

> *Prerequisites*: You have successfully built, flashed, and configured your robot.

## Open the app

There are three ways to reach the controller.

**Served by the robot.**
Open the robot's address in a browser, for example `http://spot-micro-<id>.local` or its IP address.
This app is built with `build:embedded`: it has no 3D view and no simulation, to save flash.

**Hosted.**
Open [runeharlyk.github.io/SpotMicroESP32-Leika](https://runeharlyk.github.io/SpotMicroESP32-Leika/).
The robot has no TLS, so the browser must allow an https page to reach `ws://` addresses.
Chromium does this for private IPs and `.local` names once you grant local network access.
Firefox and Safari block it, which leaves Bluetooth as the only link.

**Development server.**
Use the following commands to launch the development server with Vite, enabling instant updates:

```sh
cd app
pnpm install
pnpm proto
pnpm dev
```

`pnpm proto` generates the protobuf bindings and needs `protoc` on `PATH`.
`pnpm dev` builds the simulation models first, and serves the app on all interfaces.
Vite typically runs on port 5173, and can be accessed locally at [localhost:5173](http://localhost:5173/).

[Download the pnpm package manager from pnpm.io](https://pnpm.io/installation)

## Connect to the robot

The hosted and development apps need the robot's address.
On the start page, choose "Add a robot" and enter the hostname or IP, sweep a subnet such as `192.168.1`, or connect over Bluetooth in Chrome or Edge.
`/connection` has the same address field and a "Pair a robot" button for Bluetooth.
Bluetooth carries the controls and telemetry only; WiFi setup, file transfers and updates need the address.

## Drive the robot

Navigate to `/controller`.

The controller shows two virtual sticks, a body height slider and a row of mode buttons.
The background is the selected view: 3D representation, Stream (the camera), Split screen or Simulation.
With no robot connected, the view defaults to the simulation.

| Mode        | What the robot does                                                    |
|-------------|------------------------------------------------------------------------|
| Deactivated | Servos off                                                             |
| Idle        | No motion state; servos off                                            |
| Calibration | No motion state; servos off                                            |
| Rest        | Rest pose                                                              |
| Stand       | Stands; the left stick shifts the body, the right stick rolls and pitches it |
| Walk        | Walks; the left stick walks forward, back and sideways, the right stick turns (x) and pitches the body (y) |

In Walk, the gait buttons choose Trot or Crawl, and the "Step height" and "Speed" sliders scale the steps.
Keyboard: W, A, S and D move the left stick, and the left and right arrow keys move the right stick sideways.
A gamepad works too: the face buttons set the mode (3 deactivates, 2 rests, 1 stands, 0 walks) and the d-pad up and down step the height.

The app re-sends the sticks while they are off centre.
When the robot hears nothing for 500 ms it stops walking, so a lost connection stops the robot but holds its stance.
Press Deactivated to switch the servos off.

Servo calibration is on `/peripherals/servo` and is described in [Assembly and calibration](2_assembly.md#calibration).
