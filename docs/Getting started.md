## Prerequisites
To prepare the frontend code for the ESP32, a specific build chain is required. Start by installing these essential tools:
### Required Software
Install the following software to ensure all functionalities:

- [VSCode](https://code.visualstudio.com/) - Preferred IDE for development
- [Node.js](https://nodejs.org) - Needed for app building
- A package manager of your choice (npm, pnpm, yarn)
- [Python](https://www.python.org/downloads/) - Used for firmware build scripts

### Project Structure
Understand the project organization through these key directories:
- [docs/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/docs)  - Contains all documentation
- [app/](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/app) - SvelteKit-based frontend
- [esp32](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32) - Firmware for the robot
## Setting up PlatformIO
### Configure Build Target
Modify the `platformio.ini` file at [platformio.ini](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/master/esp32/platformio.ini) to match your board specifications. Adapt or remove environment settings as necessary based on your board.

```ini
[platformio]
...
default_envs = esp32dev
...

[env:esp32cam]
board = esp32cam
board_build.mcu = esp32c3
```

For additional boards, refer to the [official board list](https://docs.platformio.org/en/latest/boards/index.html#espressif-32).

### Build & Upload Process
Update the `platformio.ini` file for your board, then navigate to the PlatformIO tab, select your environment, click `Upload Filesystem Image` and after uploading finish, click `Upload and Monitor`. The filesystem image only has to be uploaded the first time and will override config files on the microcontroller. 
When uploading new firmware the app is evaluated and if necessary will be rebuild.

## Setting up SvelteKit
### Proxy Configuration for Development
Configure the proxy settings in the `vite.config.ts` file to direct API calls to your ESP32 device. By default it used the factory MDNS address, but can be changed to the ip if preferred.

```ts
server: {
	proxy: {
		'/api': {
			target: 'http://spot-micro.local', // Here
			changeOrigin: true,
			ws: true
		},
		'/ws': {
			target: 'ws://spot-micro.local', // Here
			changeOrigin: true,
			ws: true
		}
	}
},
```

Changes require a restart of the development server.

### Start the Development Server
Use the following commands to launch the development server with Vite, enabling instant updates:

```sh
cd app
pnpm run dev
```
Access the frontend via the provided browser link.