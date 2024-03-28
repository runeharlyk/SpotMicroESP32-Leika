export const hostname = 'localhost'; //window.location.hostname;

export const isSecure = true; // window.location.protocol === 'https:';

export const location = 'localhost:5173'; //window.location; //import.meta.env.VITE_API_URL.replace('hostname', hostname);

const socketScheme = isSecure ? 'wss://' : 'ws://';

export const socketLocation = socketScheme + location; // import.meta.env.VITE_SOCKET_URL.replace('hostname', hostname);
