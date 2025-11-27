# Developing

> _Prerequsition_: You have successfully built, flashed, and configured your robot.

## Setting up SvelteKit

### Proxy Configuration for Development

Configure the proxy settings in the `vite.config.ts` file to direct API calls to your ESP32 device. By default, it uses the factory MDNS address, but it can be changed to the IP if preferred.

```ts
server: {
    proxy: {
        '/api': {
            target: 'http://spot-micro.local', // Here
            changeOrigin: true,
            ws: true
        }
    }
},
```

> Changes require a restart of the development server.


