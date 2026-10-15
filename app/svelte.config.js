import adapter from '@sveltejs/adapter-static'
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte'

const basePath = process.env.BASE_PATH ?? ''

/** @type {import('@sveltejs/kit').Config} */
const config = {
    preprocess: vitePreprocess(),

    kit: {
        adapter: adapter({
            pages: 'build',
            assets: 'build',
            fallback: 'index.html',
            precompress: false,
            strict: true
        }),
        paths: {
            base: basePath
        },
        output: {
            // The firmware serves the embedded build, where one bundle keeps the file list short; the
            // hosted app splits so each page loads its code when it is first opened.
            bundleStrategy: process.env.PUBLIC_EMBEDDED_BUILD === 'true' ? 'single' : 'split'
        }
    }
}

export default config
