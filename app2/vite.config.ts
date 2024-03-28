import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vitest/config';
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
	}
});
