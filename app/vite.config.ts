import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import viteCompression from 'vite-plugin-compression';

// https://vitejs.dev/config/
export default defineConfig({
	plugins: [svelte(), viteSingleFile(), viteCompression({deleteOriginFile: true})],
	build: {
		outDir: '../data',
		emptyOutDir: true
	}
});
