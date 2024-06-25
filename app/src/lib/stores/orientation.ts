import { readable } from 'svelte/store';

export const heading = readable(0, (set) => {
	const updateHeading = (e: any) => {
		let alpha;
		if (e.webkitCompassHeading) alpha = e.webkitCompassHeading;
		else if (e.alpha) alpha = e.alpha;
		else {
			let q = e.target.quaternion;
			alpha =
				Math.atan2(2 * q[0] * q[1] + 2 * q[2] * q[3], 1 - 2 * q[1] * q[1] - 2 * q[2] * q[2]) *
				(180 / Math.PI);
			if (alpha < 0) alpha += 360;
		}
		set(alpha);
	};
	if ('AbsoluteOrientationSensor' in window) {
		var sensor = new window.AbsoluteOrientationSensor({ frequency: 60 }) as any;
		sensor.addEventListener('reading', updateHeading);
		sensor.start();
	} else if (window.DeviceMotionEvent) window.addEventListener('deviceorientation', updateHeading);

	return () => {
		if ('AbsoluteOrientationSensor' in window) sensor.removeEventListener('reading', updateHeading);
		window.addEventListener('deviceorientation', updateHeading);
	};
});
