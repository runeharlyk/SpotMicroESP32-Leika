import { Mesh, 
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
    LoaderUtils, 
    Object3D,
    FogExp2,
    CanvasTexture,
    type ColorRepresentation, 
    type WebGLRendererParameters,
} from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls";
import URDFLoader from "urdf-loader";
import { XacroLoader } from "xacro-parser";

export const addScene = () => new Scene()

interface position {
    x?: number,
    y?: number,
    z?: number
}

interface light {
    color?: ColorRepresentation,
    intensity?: number
}

interface gridOptions {
    divisions?: number,
    size?: number,
}

interface arrowOptions {
    origin:position,
    direction:position,
    length?:number,
    color?:ColorRepresentation
}

type directionalLight = position & light

type gridHelperOptions = gridOptions & position

export default class SceneBuilder {
    public scene: Scene
    public camera: PerspectiveCamera
    public ground: Mesh
    public renderer:WebGLRenderer
    public controls:OrbitControls
    public callback:Function
    public gridHelper: GridHelper;
    public model: Object3D<Event>
    public liveStreamTexture: CanvasTexture
    private fog:FogExp2
    private isLoaded:boolean = false

    constructor() {
        this.scene = new Scene()
        return this
    }

    public addRenderer = (parameters?: WebGLRendererParameters) => {
        this.renderer = new WebGLRenderer(parameters);
        this.renderer.outputColorSpace = "srgb";
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = PCFSoftShadowMap;
        document.body.appendChild(this.renderer.domElement);
        return this
    }

    public addPerspectiveCamera = (options:position) => {
        this.camera = new PerspectiveCamera();
        this.camera.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
        this.scene.add(this.camera);
        return this
    }

    public addGroundPlane = (options:position) => {
        this.ground = new Mesh( new PlaneGeometry(),  new ShadowMaterial({side: 2}));
        this.ground.rotation.x = -Math.PI / 2;
        this.ground.scale.setScalar(30);
        this.ground.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
        this.ground.receiveShadow = true;
        this.scene.add(this.ground);
        return this
    }

    public addOrbitControls = (minDistance:number, maxDistance:number) => {
        this.controls = new OrbitControls(this.camera, this.renderer.domElement);
        this.controls.minDistance = minDistance;
        this.controls.maxDistance = maxDistance;
        this.controls.update();
        return this
    }

    public addAmbientLight = (options:light) => {
        const ambientLight = new AmbientLight(options.color, options.intensity);
        this.scene.add(ambientLight);
        return this
    }

    public addDirectionalLight = (options:directionalLight) => {
        const directionalLight = new DirectionalLight(options.color, options.intensity);
        directionalLight.castShadow = true;
        directionalLight.shadow.mapSize.setScalar(2048);
        directionalLight.shadow.mapSize.width = 1024;
        directionalLight.shadow.mapSize.height = 1024;
        directionalLight.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
        directionalLight.shadow.radius = 5
        this.scene.add(directionalLight);
        return this
    }

    public addGridHelper = (options:gridHelperOptions) => {
        this.gridHelper = new GridHelper(options.size, options.divisions);
        this.gridHelper.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0);
        this.scene.add(this.gridHelper);
        return this
    }

    public addFogExp2 = (color:ColorRepresentation, density?:number) => {
        this.scene.fog = new FogExp2(color, density);
        return this
    }

    public handleResize = () => {
        this.renderer.setSize(window.innerWidth, window.innerHeight);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.camera.aspect = window.innerWidth / window.innerHeight;
        this.camera.updateProjectionMatrix();
        return this
    }

    public addRenderCb = (callback:Function) => {
        this.callback = callback
        return this
    }

    public startRenderLoop = () => {
        this.renderer.setAnimationLoop(() => {
            this.renderer.render(this.scene, this.camera);
            this.handleRobotShadow()
            if(this.callback) this.callback()
            if(!this.liveStreamTexture) return
        });
        return this
    }

    public addArrowHelper = (options?:arrowOptions) => {
        const dir = new Vector3(options.direction.x ?? 0, options.direction.y ?? 0, options.direction.z ?? 0);
        const origin = new Vector3(options.origin.x ?? 0, options.origin.y ?? 0, options.origin.z ?? 0);
        const arrowHelper = new ArrowHelper( dir, origin, options.length ?? 1.5, options.color ?? 0xff0000 );
        this.scene.add( arrowHelper );
        return this
    }

    public loadModel = (urlXacro:string) => {
        const xacroLoader = new XacroLoader();
        xacroLoader.load(urlXacro, xml => {
            const urdfLoader = new URDFLoader();
            urdfLoader.workingPath = LoaderUtils.extractUrlBase(urlXacro);

            this.model = urdfLoader.parse(xml);
            this.model.rotation.x = -Math.PI / 2;
            this.model.rotation.z = Math.PI / 2;
            this.model.traverse(c => c.castShadow = true);
            this.model.updateMatrixWorld(true);
            this.model.scale.setScalar(10);      

            this.scene.add(this.model);

        }, (error) => console.log(error));
        return this 
    }

    public toggleFog = () => {
        this.scene.fog = this.scene.fog ? null : this.fog;
    }

    private handleRobotShadow = () => {
        if(this.isLoaded) return
        const intervalId = setInterval(() => {
            this.model?.traverse(c => c.castShadow = true);
        }, 10);
        setTimeout(() => {
            clearInterval(intervalId)
        }, 1000);
        this.isLoaded = true;
    }
}