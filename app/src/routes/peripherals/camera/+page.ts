import type { PageLoad } from './$types';

export const load = (async () => {
	return {
		title: 'Camera'
	};
}) satisfies PageLoad;
