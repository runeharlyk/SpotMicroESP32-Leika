import type { Plugin } from 'vite';

export default function viteLittleFS(): Plugin[] {
  return [
    {
      name: 'vite-plugin-littlefs',
      enforce: 'post',
      apply: 'build',

      async config(config) {
        const output = config.build?.rollupOptions?.output;

        if (!output || !config.build?.rollupOptions) {
          return;
        }

        const outputOptions = Array.isArray(output) ? output[0] : output;

        if (!outputOptions) {
          return;
        }

        const { assetFileNames, chunkFileNames, entryFileNames } = outputOptions;

        if (assetFileNames && typeof assetFileNames === 'string') {
          config.build.rollupOptions.output = {
            ...outputOptions,
            assetFileNames: assetFileNames.replace('.[hash]', ''),
          };
        }

        if (
          chunkFileNames &&
          typeof chunkFileNames === 'string' &&
          chunkFileNames.includes('hash')
        ) {
          config.build.rollupOptions.output = {
            ...config.build.rollupOptions.output,
            chunkFileNames: chunkFileNames.replace('.[hash]', ''),
            ...(entryFileNames &&
              typeof entryFileNames === 'string' && {
                entryFileNames: entryFileNames.replace('.[hash]', ''),
              }),
          };
        }
      },
    },
  ];
}
