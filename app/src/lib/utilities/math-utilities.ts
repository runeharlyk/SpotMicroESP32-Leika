export const toUint8 = (number: number, min: number, max: number) => {
	number = Math.max(min, Math.min(max, number));
	let scaled = ((number - min) / (max - min)) * 255;
	return Math.round(scaled) & 0xff;
};

export const toInt8 = (number: number, min: number, max: number) => {
	number = Math.max(min, Math.min(max, number));
	let scaled = ((number - min) / (max - min)) * 255 - 128;
	return Math.max(-128, Math.min(127, Math.round(scaled))) | 0;
};

export const fromInt8 = (int8: number, min: number, max: number) => {
	int8 = Math.max(-128, Math.min(127, int8));
	const scaled = (int8 + 128) / 255;
	const number = scaled * (max - min) + min;
	return number;
};