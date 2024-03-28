export const humanFileSize = (size: number): string => {
	const units = ['B', 'kB', 'MB', 'GB', 'TB'];
	var i = size == 0 ? 0 : Math.floor(Math.log(size) / Math.log(1024));
	return Number((size / Math.pow(1024, i)).toFixed(2)) * 1 + units[i];
};

export const capitalize = (str: string): string => {
	return str.charAt(0).toUpperCase() + str.slice(1).toLowerCase();
};
