export const toUint8 = (number: number, min: number, max: number) => {
	number = Math.max(min, Math.min(max, number));
	let scaled = ((number - min) / (max - min)) * 255;
	return Math.round(scaled) & 0xff;
};
