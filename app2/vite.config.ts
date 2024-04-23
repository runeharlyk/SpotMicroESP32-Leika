import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';
import Icons from 'unplugin-icons/vite';
import viteLittleFS from './vite-plugin-littlefs';

export default defineConfig({
	plugins: [
		sveltekit(),
		Icons({
			compiler: 'svelte'
		}),
		viteLittleFS()
	],
	test: {
		include: ['src/**/*.{test,spec}.{js,ts}']
	},
	server: {
		proxy: {
			'/api': {
				target: 'http://192.168.0.172',
				changeOrigin: true,
				ws: true
			},
			'/ws': {
				target: 'ws://192.168.0.172',
				changeOrigin: true,
				ws: true
			}
		}
	}
});
