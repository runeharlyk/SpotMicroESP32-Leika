/** @type {import('tailwindcss').Config} */
export default {
	content: ['./src/**/*.{html,js,ts,svelte}'],
	theme: {
        extend: {
          colors: {
            'primary': '#6200EE',
            'primary-variant': '#3700B3',
            'secondary': '#3700B3',
            'secondary-variant': '#3700B3',
            'background': '#1e1e1e',
            'surface': '#2c2c2c',
            'error': '#B00020',
            'on-primary': '#FFFFFF',
            'on-secondary': '#FFFFFF',
            'on-background': '#FFFFFF',
            'on-surface': '#FFFFFF',
            'on-error': '#FFFFFF',
          }
        },
	},
	plugins: []
};
