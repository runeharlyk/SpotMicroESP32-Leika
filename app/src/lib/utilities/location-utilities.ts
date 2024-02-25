export const webAppBuild = import.meta.env.MODE === 'WEB';
export const hostname = window.location.hostname;

export const isSecure = window.location.protocol === 'https:';

export const location = import.meta.env.VITE_API_URL.replace('hostname', hostname);

const socketScheme = isSecure ? 'wss://' : 'ws://';

export const socketLocation =
	socketScheme + import.meta.env.VITE_SOCKET_URL.replace('hostname', hostname);
