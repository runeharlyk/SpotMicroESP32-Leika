import { sveltekit } from '@sveltejs/kit/vite'
import { defineConfig } from 'vite'
import Icons from 'unplugin-icons/vite'
import viteLittleFS from './vite-plugin-littlefs'
import tailwindcss from '@tailwindcss/vite'

const basePath = process.env.BASE_PATH ?? ''

// The firmware's built-in app has no simulation. Left external there, MuJoCo's loader is never
// transformed, so the 10 MB WASM it references is not emitted into the flash image.
const embeddedBuild = process.env.PUBLIC_EMBEDDED_BUILD === 'true'

export default defineConfig({
    base: basePath,
    plugins: [
        tailwindcss(),
        sveltekit(),
        Icons({
            compiler: 'svelte'
        }),
        viteLittleFS()
    ],
    build: {
        rollupOptions: { external: embeddedBuild ? [/^@mujoco\/mujoco/] : [] }
    },
    server: {
        proxy: {
            '/api': {
                target: 'http://spot-micro.local/',
                changeOrigin: true,
                ws: true
            }
        }
    }
})
