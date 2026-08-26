import { defineConfig, UserConfigExport } from 'vitest/config'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import path from 'path'

const config: UserConfigExport = {
    plugins: [svelte()],
    resolve: {
        alias: {
            $lib: path.resolve(__dirname, './src/lib'),
            '$app/paths': path.resolve(__dirname, './tests/stubs/app-paths.ts'),
            '$app/environment': path.resolve(__dirname, './tests/stubs/app-environment.ts'),
            '$env/static/public': path.resolve(__dirname, './tests/stubs/env-static-public.ts')
        }
    },
    test: {
        globals: true,
        environment: 'jsdom'
    }
}
export default defineConfig(config)
