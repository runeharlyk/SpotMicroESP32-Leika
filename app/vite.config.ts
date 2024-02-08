import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import viteCompression from 'vite-plugin-compression';
import path from 'path'

const forEmbedded = process.env.FOR_EMBEDDED == 'true'

// https://vitejs.dev/config/
export default defineConfig({
	plugins: [svelte(), 
        ...(forEmbedded ? [ viteSingleFile(), viteCompression({deleteOriginFile: true})]: [])],
	build: {
		outDir: forEmbedded ? '../data': './build',
		emptyOutDir: true
	},
    resolve: {
        alias: {
            '$lib': path.resolve('./src/lib/'),
            '$components': path.resolve('./src/components'),
            '$utils': path.resolve('./src/utils'),
            '$stores': path.resolve('./src/stores'),
        },
    },
});
