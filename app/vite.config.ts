import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import viteCompression from 'vite-plugin-compression';

const forEmbedded = process.env.FOR_EMBEDDED == 'true'

// https://vitejs.dev/config/
export default defineConfig({
	plugins: [svelte(), 
        ...(forEmbedded ? [ viteSingleFile(), viteCompression({deleteOriginFile: true})]: [])],
	build: {
		outDir: forEmbedded ? '../data': './build',
		emptyOutDir: true
	}
});
