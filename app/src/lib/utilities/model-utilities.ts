import { Color, LoaderUtils, Vector3 } from 'three';
import URDFLoader, { type URDFRobot } from 'urdf-loader';
import { XacroLoader } from 'xacro-parser';
import { Result } from '$lib/utilities';
import { jointNames, model } from '$lib/stores';
import uzip from 'uzip';
import { fileService } from '$lib/services';

let model_xml: XMLDocument;

export const populateModelCache = async () => {
	await cacheModelFiles();
	const modelRes = await loadModelAsync('/spot_micro.urdf.xacro');
	if (modelRes.isOk()) {
		const [urdf, JOINT_NAME] = modelRes.inner;
		jointNames.set(JOINT_NAME);
		model.set(urdf);
	} else {
		console.error(modelRes.inner, { exception: modelRes.exception });
	}
};

export const cacheModelFiles = async () => {
	let data = await fetch('/stl.zip');

	var files = uzip.parse(await data.arrayBuffer());

	for (const [path, data] of Object.entries(files) as [path: string, data: Uint8Array][]) {
		const url = new URL(path, window.location.href);
		fileService.saveFile(url.toString(), data);
	}
};

export const loadModelAsync = async (
	url: string
): Promise<Result<[URDFRobot, string[]], string>> => {
	return new Promise((resolve, reject) => {
		const xacroLoader = new XacroLoader();
		const urdfLoader = new URDFLoader();
		urdfLoader.workingPath = LoaderUtils.extractUrlBase(url);

		xacroLoader.load(
			url,
			async (xml) => {
				model_xml = xml;
				try {
					const model = urdfLoader.parse(xml);
					model.rotation.x = -Math.PI / 2;
					model.rotation.z = Math.PI / 2;
					model.traverse((c) => (c.castShadow = true));
					model.updateMatrixWorld(true);
					model.scale.setScalar(10);
					const joints = Object.entries(model.joints)
						.filter((joint) => joint[1].jointType !== 'fixed')
						.map((joint) => joint[0]);

					resolve(Result.ok([model, joints]));
				} catch (error) {
					resolve(Result.err('Failed to load model', error));
				}
			},
			(error) => resolve(Result.err('Failed to load model', error))
		);
	});
};

export const toeWorldPositions = (robot: URDFRobot) => {
	const toe_positions: Vector3[] = [];
	robot.traverse((child) => {
		if (child.name.includes('toe') && !child.name.includes('_link')) {
			const worldPosition = new Vector3();
			child.getWorldPosition(worldPosition);
			toe_positions.push(worldPosition);
		}
	});
	return toe_positions;
};

export const footColor = () => {
	const colorElem = model_xml.querySelector('material[name=foot_color] > color') as Element;
	const colorAttrStr = colorElem.getAttribute('rgba') as string;
	const colorStr = colorAttrStr
		.split(' ')
		.slice(0, 3)
		.map((val) => Math.floor(+val * 255))
		.join(', ');

	return new Color(`rgb(${colorStr})`);
};
