import { jointNames, model } from '$lib/stores';
import { loadModelAsync } from '$lib/utilities/model-utilities';
import type { Result } from '$lib/utilities/result';

export const prerender = true;
export const ssr = false;

const registerFetchIntercept = async () => {
	if (typeof WebSocket === 'undefined') return;
	const { fetch: originalFetch } = window;
	const fileService = (await import('$lib/services/file-service')).default;
	window.fetch = async (...args) => {
		const [resource, config] = args;
		let file: Result<Uint8Array | undefined, string>;
		file = await fileService.getFile(resource.toString());
		return file.isOk() ? new Response(file.inner) : originalFetch(resource, config);
	};
};

const setup = async () => {
	if (typeof WebSocket === 'undefined') return;
	const outControllerData = (await import('$lib/stores/model-store')).outControllerData;
	const mode = (await import('$lib/stores/model-store')).mode;
	const socketLocation = (await import('$lib/utilities/location-utilities')).socketLocation;
	const socketService = (await import('$lib/services/socket-service')).default;
	socketService.connect(socketLocation);
	socketService.addPublisher(outControllerData);
	socketService.addPublisher(mode, 'mode');
	await registerFetchIntercept();
	const modelRes = await loadModelAsync('/spot_micro.urdf.xacro');

	if (modelRes.isOk()) {
		const [urdf, JOINT_NAME] = modelRes.inner;
		jointNames.set(JOINT_NAME);
		model.set(urdf);
	} else {
		console.error(modelRes.inner, { exception: modelRes.exception });
	}
};

export const load = async () => {
	await setup();
	// const result = await fetch('/rest/features');
	const item = {}; //await result.json();
	return {
		features: item,
		title: 'spot-micro-controller',
		github: 'runeharlyk/spotmicro'
	};
};
