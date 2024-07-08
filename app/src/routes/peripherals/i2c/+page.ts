import type { PageLoad } from './$types';

export const load = (async () => {
	return {
		title: 'I2C'
	};
}) satisfies PageLoad;
