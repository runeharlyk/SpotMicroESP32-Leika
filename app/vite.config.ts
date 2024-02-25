import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import viteCompression from 'vite-plugin-compression';
import path from 'path';
import 'dotenv/config';

const embeddedBuild = process.env.VITE_EMBEDDED_BUILD == 'true';

// https://vitejs.dev/config/
export default defineConfig({
	plugins: [
		svelte(),
		...(embeddedBuild ? [viteSingleFile(), viteCompression({ deleteOriginFile: true })] : [])
	],
	build: {
		outDir: embeddedBuild ? '../data' : './build',
		emptyOutDir: true
	},
	resolve: {
		alias: {
			$lib: path.resolve('./src/lib/'),
			$components: path.resolve('./src/components'),
			$utils: path.resolve('./src/utils'),
			$stores: path.resolve('./src/stores')
		}
	}
});
