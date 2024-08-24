import type { PageLoad } from './$types';

export const load = (async () => {
	return {
		title: 'Connection'
	};
}) satisfies PageLoad;
