import type { PageLoad } from './$types';

export const load = (async () => {
	return { title: 'File System' };
}) satisfies PageLoad;
