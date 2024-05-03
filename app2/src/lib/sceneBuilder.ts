import {
	Mesh,
	PerspectiveCamera,
	PlaneGeometry,
	Scene,
	ShadowMaterial,
	WebGLRenderer,
	AmbientLight,
	DirectionalLight,
	PCFSoftShadowMap,
	GridHelper,
	ArrowHelper,
	Vector3,
	FogExp2,
	CanvasTexture,
	type ColorRepresentation,
	type WebGLRendererParameters,
	MeshPhongMaterial,
	EquirectangularReflectionMapping,
	ACESFilmicToneMapping,
	MathUtils
} from 'three';
import { Sky } from 'three/addons/objects/Sky.js';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls';
import { type URDFJoint, type URDFMimicJoint, type URDFRobot } from 'urdf-loader';
import { PointerURDFDragControls } from 'urdf-loader/src/URDFDragControls';

export const addScene = () => new Scene();

interface position {
	x?: number;
	y?: number;
	z?: number;
}

interface light {
	color?: ColorRepresentation;
	intensity?: number;
}

interface gridOptions {
	divisions?: number;
	size?: number;
}

interface arrowOptions {
	origin: position;
	direction: position;
	length?: number;
	color?: ColorRepresentation;
}

type directionalLight = position & light;

type gridHelperOptions = gridOptions & position;

function calculateCurrentSunElevation() {
	let now = new Date();
	let decimalTime = now.getHours() + now.getMinutes() / 60;
	let normalizedTime = (decimalTime % 12) / 6 - 1;
	return 10 * Math.sin(normalizedTime * Math.PI);
}

export default class SceneBuilder {
	public scene: Scene;
	public camera: PerspectiveCamera;
	public ground: Mesh;
	public renderer: WebGLRenderer;
	public controls: OrbitControls;
	public callback: Function;
	public gridHelper: GridHelper;
	public model: URDFRobot;
	public liveStreamTexture: CanvasTexture;
	private fog: FogExp2;
	private isLoaded: boolean = false;
	highlightMaterial: any;

	constructor() {
		this.scene = new Scene();
		if (this.scene.environment?.mapping) {
			this.scene.environment.mapping = EquirectangularReflectionMapping;
		}
		return this;
	}

	public addRenderer = (parameters?: WebGLRendererParameters) => {
		this.renderer = new WebGLRenderer(parameters);
		this.renderer.outputColorSpace = 'srgb';
		this.renderer.shadowMap.enabled = true;
		this.renderer.shadowMap.type = PCFSoftShadowMap;
		this.renderer.toneMapping = ACESFilmicToneMapping;
		this.renderer.toneMappingExposure = 0.85;
		if (!parameters.canvas) document.body.appendChild(this.renderer.domElement);
		return this;
	};

	public addSky = () => {
		const sky = new Sky();
		sky.scale.setScalar(450000);
		this.scene.add(sky);
		const effectController = {
			turbidity: 10,
			rayleigh: 3,
			mieCoefficient: 0.005,
			mieDirectionalG: 0.7,
			elevation: calculateCurrentSunElevation(),
			azimuth: 180,
			exposure: this.renderer.toneMappingExposure
		};
		const uniforms = sky.material.uniforms;
		uniforms['turbidity'].value = effectController.turbidity;
		uniforms['rayleigh'].value = effectController.rayleigh;
		uniforms['mieCoefficient'].value = effectController.mieCoefficient;
		uniforms['mieDirectionalG'].value = effectController.mieDirectionalG;
		this.renderer.toneMappingExposure = 0.5;
		const phi = MathUtils.degToRad(90 - effectController.elevation);
		const theta = MathUtils.degToRad(effectController.azimuth);
		const sun = new Vector3();

		sun.setFromSphericalCoords(1, phi, theta);
		uniforms['sunPosition'].value.copy(sun);
		return this;
	};

	public addPerspectiveCamera = (options: position) => {
		this.camera = new PerspectiveCamera();
		this.camera.position.set(options.x ?? 0, options.y ?? 2.7, options.z ?? 0);
		this.scene.add(this.camera);
		return this;
	};

	public addGroundPlane = (options?: position) => {
		this.ground = new Mesh(new PlaneGeometry(), new ShadowMaterial({ side: 2 }));
		this.ground.rotation.x = -Math.PI / 2;
		this.ground.scale.setScalar(30);
		this.ground.position.set(options?.x ?? 0, options?.y ?? 0, options?.z ?? 0);
		this.ground.receiveShadow = true;
		this.scene.add(this.ground);
		return this;
	};

	public addOrbitControls = (minDistance: number, maxDistance: number, autoRotate = true) => {
		this.controls = new OrbitControls(this.camera, this.renderer.domElement);
		this.controls.minDistance = minDistance;
		this.controls.maxDistance = maxDistance;
		this.controls.autoRotate = autoRotate;
		this.controls.update();
		return this;
	};

	public addAmbientLight = (options: light) => {
		const ambientLight = new AmbientLight(options.color, options.intensity);
		this.scene.add(ambientLight);
		return this;
	};

	public addDirectionalLight = (options: directionalLight) => {
		const directionalLight = new DirectionalLight(options.color, options.intensity);
		directionalLight.castShadow = true;
		directionalLight.shadow.mapSize.setScalar(2048);
		directionalLight.shadow.mapSize.width = 1024;
		directionalLight.shadow.mapSize.height = 1024;
		directionalLight.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
		directionalLight.shadow.radius = 5;
		this.scene.add(directionalLight);
		return this;
	};

	public addGridHelper = (options: gridHelperOptions) => {
		this.gridHelper = new GridHelper(options.size, options.divisions);
		this.gridHelper.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
		this.gridHelper.material.opacity = 0.2;
		this.gridHelper.material.depthWrite = false;
		this.gridHelper.material.transparent = true;
		this.scene.add(this.gridHelper);
		return this;
	};

	public addFogExp2 = (color: ColorRepresentation, density?: number) => {
		this.scene.fog = new FogExp2(color, density);
		return this;
	};

	public fillParent = () => {
		const parentElement = this.renderer.domElement.parentElement;
		if (parentElement) {
			const width = parentElement.clientWidth;
			const height = parentElement.clientHeight;
			this.handleResize(width, height);
		}
		return this;
	};

	public handleResize = (width = window.innerWidth, height = window.innerHeight) => {
		this.renderer.setSize(width, height);
		this.renderer.setPixelRatio(window.devicePixelRatio);
		this.camera.aspect = width / height;
		this.camera.updateProjectionMatrix();
		return this;
	};

	public addRenderCb = (callback: Function) => {
		this.callback = callback;
		return this;
	};

	public startRenderLoop = () => {
		this.renderer.setAnimationLoop(() => {
			this.renderer.render(this.scene, this.camera);
			this.controls.update();
			this.handleRobotShadow();
			if (this.callback) this.callback();
			if (!this.liveStreamTexture) return;
		});
		return this;
	};

	public addArrowHelper = (options?: arrowOptions) => {
		const dir = new Vector3(
			options?.direction.x ?? 0,
			options?.direction.y ?? 0,
			options?.direction.z ?? 0
		);
		const origin = new Vector3(
			options?.origin.x ?? 0,
			options?.origin.y ?? 0,
			options?.origin.z ?? 0
		);
		const arrowHelper = new ArrowHelper(
			dir,
			origin,
			options?.length ?? 1.5,
			options?.color ?? 0xff0000
		);
		this.scene.add(arrowHelper);
		return this;
	};

	private setJointValue(jointName: string, angle: number) {
		if (!this.model) return;
		if (!this.model.joints[jointName]) return;
		this.model.joints[jointName].setJointValue(angle);
	}

	isJoint = (j: URDFJoint) => j.isURDFJoint && j.jointType !== 'fixed';

	highlightLinkGeometry = (m: URDFMimicJoint, revert: boolean, material: MeshPhongMaterial) => {
		const traverse = (c: any) => {
			if (c.type === 'Mesh') {
				if (revert) {
					c.material = c.__origMaterial;
					delete c.__origMaterial;
				} else {
					c.__origMaterial = c.material;
					c.material = material;
				}
			}

			if (c === m || !this.isJoint(c)) {
				for (let i = 0; i < c.children.length; i++) {
					const child = c.children[i];
					if (!child.isURDFCollider) {
						traverse(c.children[i]);
					}
				}
			}
		};
		traverse(m);
	};

	public addModel = (model: any) => {
		this.model = model;
		this.scene.add(model);
		return this;
	};

	public addDragControl = (updateAngle: any) => {
		const highlightColor = '#FFFFFF';
		const highlightMaterial = new MeshPhongMaterial({
			shininess: 10,
			color: highlightColor,
			emissive: highlightColor,
			emissiveIntensity: 0.25
		});

		const dragControls = new PointerURDFDragControls(
			this.scene,
			this.camera,
			this.renderer.domElement
		);
		dragControls.updateJoint = (joint: URDFMimicJoint, angle: number) => {
			this.setJointValue(joint.name, angle);
			updateAngle(joint.name, angle);
		};
		dragControls.onDragStart = () => (this.controls.enabled = false);
		dragControls.onDragEnd = () => (this.controls.enabled = true);
		dragControls.onHover = (joint: URDFMimicJoint) =>
			this.highlightLinkGeometry(joint, false, highlightMaterial);
		dragControls.onUnhover = (joint: URDFMimicJoint) =>
			this.highlightLinkGeometry(joint, true, highlightMaterial);

		this.renderer.domElement.addEventListener('touchstart', (data) =>
			dragControls._mouseDown(data.touches[0])
		);
		this.renderer.domElement.addEventListener('touchmove', (data) =>
			dragControls._mouseMove(data.touches[0])
		);
		this.renderer.domElement.addEventListener('touchend', (data) =>
			dragControls._mouseUp(data.touches[0])
		);
		return this;
	};

	public toggleFog = () => {
		this.scene.fog = this.scene.fog ? null : this.fog;
	};

	private handleRobotShadow = () => {
		if (this.isLoaded) return;
		const intervalId = setInterval(() => {
			this.model?.traverse((c) => (c.castShadow = true));
		}, 10);
		setTimeout(() => {
			clearInterval(intervalId);
		}, 1000);
		this.isLoaded = true;
	};
}
