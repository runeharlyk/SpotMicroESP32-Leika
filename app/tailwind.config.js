/** @type {import('tailwindcss').Config} */
export default {
	content: ['./src/**/*.{html,js,ts,svelte}'],
	theme: {
		extend: {}
	},
	plugins: [require('daisyui')],
	daisyui: {
		themes: [
			'light',
			{
				dark: {
					...require('daisyui/src/theming/themes')['dark'],
					primary: '#00bfff',
					accent: '#3c00ff'
				}
			}
		],
		darkTheme: 'dark'
	}
};
