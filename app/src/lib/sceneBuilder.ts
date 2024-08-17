import {
	Mesh,
	PerspectiveCamera,
	PlaneGeometry,
	Scene,
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
	MathUtils,
	Group,
	MeshBasicMaterial,
	RepeatWrapping
} from 'three';
import { Sky } from 'three/addons/objects/Sky.js';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls';
import { Reflector } from 'three/examples/jsm/objects/Reflector.js';
import { type URDFJoint, type URDFMimicJoint, type URDFRobot } from 'urdf-loader';
import { PointerURDFDragControls } from 'urdf-loader/src/URDFDragControls';
import { sunCalculator } from './utilities/position-utilities';

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

interface arrowOptions {
	origin: position;
	direction: position;
	length?: number;
	color?: ColorRepresentation;
}

type directionalLight = position & light;

export default class SceneBuilder {
	public scene: Scene;
	public camera!: PerspectiveCamera;
	public ground!: Mesh;
	public renderer!: WebGLRenderer;
	public orbit: OrbitControls;
	public callback: Function | undefined;
	public gridHelper!: GridHelper;
	public model!: URDFRobot;
	public liveStreamTexture!: CanvasTexture;
	private fog!: FogExp2;
	private isLoaded: boolean = false;
	public isDragging: boolean = false;
	highlightMaterial: any;
	sky!: Sky;
	transformControl: TransformControls;
	public modelGroup!: Group;

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
		if (!parameters?.canvas) document.body.appendChild(this.renderer.domElement);
		return this;
	};

	public addSky = () => {
		this.sky = new Sky();
		this.sky.scale.setScalar(450000);
		this.scene.add(this.sky);
		const effectController = {
			turbidity: 10,
			rayleigh: 3,
			mieCoefficient: 0.005,
			mieDirectionalG: 0.7,
			elevation: sunCalculator.calculateSunElevation(),
			azimuth: 200,
			exposure: this.renderer.toneMappingExposure
		};
		const uniforms = this.sky.material.uniforms;
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
		const checkerboardTexture = this.createCheckerboardTexture(1024, 2);
		checkerboardTexture.wrapS = RepeatWrapping;
		checkerboardTexture.wrapT = RepeatWrapping;
		checkerboardTexture.repeat.set(100, 100);
		const checkerboardMat = new MeshBasicMaterial({
			map: checkerboardTexture,
			opacity: 0.1,
			transparent: true
		});

		const plane = new PlaneGeometry(400, 400);

		this.ground = new Mesh(plane, checkerboardMat);
		this.ground.rotation.x = -Math.PI / 2;
		this.ground.position.set(options?.x ?? 0, options?.y ?? 0.01, options?.z ?? 0);
		this.ground.receiveShadow = true;
		this.scene.add(this.ground);

		const mirror = new Reflector(plane, {
			clipBias: 0.003,
			textureWidth: window.innerWidth * window.devicePixelRatio,
			textureHeight: window.innerHeight * window.devicePixelRatio,
			color: 0x00bfff
		});
		mirror.rotateX(-Math.PI / 2);
		this.scene.add(mirror);

		return this;
	};

	public addOrbitControls = (minDistance: number, maxDistance: number, autoRotate = true) => {
		this.orbit = new OrbitControls(this.camera, this.renderer.domElement);
		this.orbit.minDistance = minDistance;
		this.orbit.maxDistance = maxDistance;
		this.orbit.autoRotate = autoRotate;
		this.orbit.update();
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
		directionalLight.shadow.camera.top = 10;
		directionalLight.shadow.camera.bottom = -10;
		directionalLight.shadow.camera.right = 10;
		directionalLight.shadow.camera.left = -10;
		directionalLight.shadow.mapSize.set(4096, 4096);

		directionalLight.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
		this.scene.add(directionalLight);
		return this;
	};

	private createCheckerboardTexture = (size: number, squares: number) => {
		const canvas = document.createElement('canvas');
		canvas.width = size;
		canvas.height = size;
		const context = canvas.getContext('2d');

		const squareSize = size / squares;

		for (let y = 0; y < squares; y++) {
			for (let x = 0; x < squares; x++) {
				context!.fillStyle = (x + y) % 2 === 0 ? '#ffffff' : '#000000';
				context!.fillRect(x * squareSize, y * squareSize, squareSize, squareSize);
			}
		}

		const texture = new CanvasTexture(canvas);
		texture.wrapS = texture.wrapT = RepeatWrapping;
		texture.anisotropy = 16;
		return texture;
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
			this.orbit.update();
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

	public addTransformControls = (model: any) => {
		this.transformControl = new TransformControls(this.camera, this.renderer.domElement);
		this.transformControl.addEventListener('dragging-changed', (event: any) => {
			this.orbit.enabled = !event.value;
			this.isDragging = !event.value;
		});
		this.transformControl.attach(model);
		this.scene.add(this.transformControl);
		this.transformControl.setMode('rotate');
		return this;
	};

	public addModel = (model: any) => {
		this.modelGroup = new Group();
		this.modelGroup.add(model);
		this.model = model;
		this.scene.add(this.modelGroup);
		return this;
	};

	public addDragControl = (updateAngle: any) => {
		const highlightColor = '#FFFFFF';
		const highlightMaterial = new MeshPhongMaterial({
			shininess: 10,
			color: highlightColor,
			emissive: highlightColor,
			emissiveIntensity: 0.9
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
		dragControls.onDragStart = () => {
			this.orbit.enabled = false;
			this.isDragging = true;
		};
		dragControls.onDragEnd = () => {
			this.orbit.enabled = true;
			this.isDragging = false;
		};
		dragControls.onHover = (joint: URDFMimicJoint) =>
			this.highlightLinkGeometry(joint, false, highlightMaterial);
		dragControls.onUnhover = (joint: URDFMimicJoint) =>
			this.highlightLinkGeometry(joint, true, highlightMaterial);

		this.renderer.domElement.addEventListener(
			'touchstart',
			(data) => dragControls._mouseDown(data.touches[0]),
			{ passive: true }
		);
		this.renderer.domElement.addEventListener(
			'touchmove',
			(data) => dragControls._mouseMove(data.touches[0]),
			{ passive: true }
		);
		this.renderer.domElement.addEventListener(
			'touchend',
			(data) => dragControls._mouseUp(data.touches[0]),
			{ passive: true }
		);
		return this;
	};

	public toggleFog = () => {
		this.scene.fog = this.scene.fog ? null : this.fog;
	};

	private handleRobotShadow = () => {
		if (this.isLoaded) return;
		const intervalId = setInterval(() => this.model?.traverse((c) => (c.castShadow = true)), 10);
		setTimeout(() => clearInterval(intervalId), 1000);
		this.isLoaded = true;
	};
}
