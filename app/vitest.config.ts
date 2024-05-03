import { defineConfig, UserConfigExport } from 'vitest/config'
import { svelte } from '@sveltejs/vite-plugin-svelte';


const config: UserConfigExport = {
	plugins: [svelte()],
	test: {
		globals: true,
		environment: 'jsdom'
	}
};
export default defineConfig(config)