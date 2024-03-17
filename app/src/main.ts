import './app.css';
import './index.css';
import App from './App.svelte';

if ('serviceWorker' in navigator) {
	window.addEventListener('load', () => {
		navigator.serviceWorker.register('/sw.js');
	});
}

const app = new App({
	target: document.getElementById('app') as HTMLElement
});

export default app;
