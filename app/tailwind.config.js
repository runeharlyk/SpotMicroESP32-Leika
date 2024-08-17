/** @type {import('tailwindcss').Config} */
export default {
	content: ['./src/**/*.{html,js,ts,svelte}'],
	theme: {
		extend: {}
	},
	plugins: [require('daisyui')],
	daisyui: {
		themes: [
			{
				light: {
					...require('daisyui/src/theming/themes')['light'],
					primary: '#00bfff',
					accent: '#3c00ff',
					'base-content': 'oklch(0.3 0.012 256)'
				}
			},
			{
				dark: {
					...require('daisyui/src/theming/themes')['dark'],
					primary: '#00bfff',
					accent: '#3c00ff',
					'base-content': 'white'
				}
			}
		],
		darkTheme: 'dark'
	}
};
