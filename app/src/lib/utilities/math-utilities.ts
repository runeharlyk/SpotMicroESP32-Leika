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