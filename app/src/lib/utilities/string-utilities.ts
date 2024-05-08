export const humanFileSize = (size: number): string => {
	const units = ['B', 'kB', 'MB', 'GB', 'TB'];
	var i = size == 0 ? 0 : Math.floor(Math.log(size) / Math.log(1024));
	return Number((size / Math.pow(1024, i)).toFixed(2)) * 1 + units[i];
};

export const capitalize = (str: string): string => {
	return str.charAt(0).toUpperCase() + str.slice(1).toLowerCase();
};

export const convertSeconds = (seconds: number) => {
	// Calculate the number of seconds, minutes, hours, and days
	let minutes = Math.floor(seconds / 60);
	let hours = Math.floor(minutes / 60);
	let days = Math.floor(hours / 24);

	// Calculate the remaining hours, minutes, and seconds
	hours = hours % 24;
	minutes = minutes % 60;
	seconds = seconds % 60;

	// Create the formatted string
	let result = '';
	if (days > 0) {
		result += days + ' day' + (days > 1 ? 's' : '') + ' ';
	}
	if (hours > 0) {
		result += hours + ' hour' + (hours > 1 ? 's' : '') + ' ';
	}
	if (minutes > 0) {
		result += minutes + ' minute' + (minutes > 1 ? 's' : '') + ' ';
	}
	result += seconds + ' second' + (seconds > 1 ? 's' : '');

	return result;
};