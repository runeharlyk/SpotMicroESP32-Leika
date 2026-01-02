import { defineConfig, UserConfigExport } from 'vitest/config'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import path from 'path'

const config: UserConfigExport = {
    plugins: [svelte()],
    resolve: {
        alias: {
            $lib: path.resolve(__dirname, './src/lib')
        }
    },
    test: {
        globals: true,
        environment: 'jsdom'
    }
}
export default defineConfig(config)
