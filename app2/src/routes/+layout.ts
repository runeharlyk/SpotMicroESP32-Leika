import { jointNames, model } from '$lib/stores';
import { loadModelAsync } from '$lib/utilities/model-utilities';

export const prerender = true;
export const ssr = false;

const registerFetchIntercept = async () => {
	const { fetch: originalFetch } = window;
	const fileService = (await import('$lib/services/file-service')).default;
	window.fetch = async (resource, config) => {
		let url = resource instanceof Request ? resource.url : resource.toString();
		let file = await fileService.getFile(url);
		return file.isOk() ? new Response(file.inner) : originalFetch(resource, config);
	};
};

const setup = async () => {
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
	const result = await fetch('/rest/features');
	const features = await result.json();
	return {
		features,
		title: 'Spot micro controller',
		github: 'runeharlyk/SpotMicroESP32-Leika',
		app_name: 'Spot Micro Controller',
		copyright: '2024 Rune Harlyk'
	};
};
