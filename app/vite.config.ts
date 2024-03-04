import { defineConfig, loadEnv } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import viteCompression from 'vite-plugin-compression';
import path from 'path';

// https://vitejs.dev/config/
export default ({ mode }) => {
	process.env = { ...process.env, ...loadEnv(mode, process.cwd()) };
	const embeddedBuild = process.env.VITE_EMBEDDED_BUILD == 'true';

	return defineConfig({
		plugins: [
			svelte(),
			...(embeddedBuild ? [viteSingleFile(), viteCompression({ deleteOriginFile: true })] : [])
		],
		build: {
			outDir: embeddedBuild ? '../esp32/data' : './build',
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
};
