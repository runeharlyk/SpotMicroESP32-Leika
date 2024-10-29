import daisyui from 'daisyui';
import { light, dark } from 'daisyui/src/theming/themes';

/** @type {import('tailwindcss').Config} */
export default {
    content: ['./src/**/*.{html,js,ts,svelte}'],
    theme: {
        extend: {}
    },
    plugins: [daisyui],
    daisyui: {
        themes: [
            {
                light: {
                    ...light,
                    primary: '#00bfff',
                    accent: '#3c00ff',
                    'base-content': 'oklch(0.3 0.012 256)'
                }
            },
            {
                dark: {
                    ...dark,
                    primary: '#00bfff',
                    accent: '#3c00ff',
                    'base-content': 'white'
                }
            }
        ],
        darkTheme: 'dark'
    }
};
