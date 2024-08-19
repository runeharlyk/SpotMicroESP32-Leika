export const daisyColor = (name: string, opacity: number = 100) => {
	const color = getComputedStyle(document.documentElement).getPropertyValue(name);
	return `oklch(${color} / ${opacity}%)`;
};
