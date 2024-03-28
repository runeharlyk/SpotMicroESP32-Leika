import { Color, LoaderUtils, Vector3 } from 'three';
import URDFLoader, { type URDFRobot } from 'urdf-loader';
import { XacroLoader } from 'xacro-parser';
import { Result } from '$lib/utilities';

let model_xml: XMLDocument;

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
			(error) => reject(error)
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
