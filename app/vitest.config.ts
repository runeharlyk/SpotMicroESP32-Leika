import { defineConfig, UserConfigExport } from 'vitest/config'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import path from 'path'
import Icons from 'unplugin-icons/vite'

const config: UserConfigExport = {
    plugins: [svelte(), Icons({ compiler: 'svelte' })],
    resolve: {
        // Svelte's client build is needed to mount components; the browser condition would also
        // hand the tests' WebSocket server the ws package's browser stub, so ws keeps its Node entry.
        conditions: ['browser'],
        alias: {
            ws: path.resolve(__dirname, './node_modules/ws/wrapper.mjs'),
            $lib: path.resolve(__dirname, './src/lib'),
            '$app/paths': path.resolve(__dirname, './tests/stubs/app-paths.ts'),
            '$app/state': path.resolve(__dirname, './tests/stubs/app-state.svelte.ts'),
            '$app/environment': path.resolve(__dirname, './tests/stubs/app-environment.ts'),
            '$env/static/public': path.resolve(__dirname, './tests/stubs/env-static-public.ts')
        }
    },
    test: {
        globals: true,
        environment: 'jsdom',
        setupFiles: ['./tests/setup.ts']
    }
}
export default defineConfig(config)
