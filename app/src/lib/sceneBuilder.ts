import {
    Mesh,
    PerspectiveCamera,
    PlaneGeometry,
    Scene,
    WebGLRenderer,
    AmbientLight,
    DirectionalLight,
    PCFSoftShadowMap,
    type GridHelper,
    ArrowHelper,
    Vector3,
    FogExp2,
    CanvasTexture,
    type ColorRepresentation,
    type WebGLRendererParameters,
    EquirectangularReflectionMapping,
    ACESFilmicToneMapping,
    Group,
    MeshBasicMaterial,
    RepeatWrapping
} from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { Reflector } from 'three/examples/jsm/objects/Reflector.js'
import { type URDFRobot } from 'urdf-loader'

interface position {
    x?: number
    y?: number
    z?: number
}

interface light {
    color?: ColorRepresentation
    intensity?: number
}

interface arrowOptions {
    origin: position
    direction: position
    length?: number
    color?: ColorRepresentation
}

type directionalLight = position & light

export default class SceneBuilder {
    public scene: Scene
    public camera!: PerspectiveCamera
    public ground!: Mesh
    public renderer!: WebGLRenderer
    public orbit!: OrbitControls
    public callback: (() => void) | undefined
    public gridHelper!: GridHelper
    public model!: URDFRobot
    public liveStreamTexture!: CanvasTexture
    private fog!: FogExp2
    private isLoaded: boolean = false
    public modelGroup!: Group

    constructor() {
        this.scene = new Scene()
        if (this.scene.environment?.mapping) {
            this.scene.environment.mapping = EquirectangularReflectionMapping
        }
        return this
    }

    public addRenderer = (parameters?: WebGLRendererParameters) => {
        this.renderer = new WebGLRenderer(parameters)
        this.renderer.outputColorSpace = 'srgb'
        this.renderer.shadowMap.enabled = true
        this.renderer.shadowMap.type = PCFSoftShadowMap
        this.renderer.toneMapping = ACESFilmicToneMapping
        this.renderer.toneMappingExposure = 0.85
        if (!parameters?.canvas) document.body.appendChild(this.renderer.domElement)
        return this
    }

    public addPerspectiveCamera = (options: position) => {
        this.camera = new PerspectiveCamera()
        this.camera.position.set(options.x ?? 0, options.y ?? 2.7, options.z ?? 0)
        this.scene.add(this.camera)
        return this
    }

    public addGroundPlane = (options?: position) => {
        const checkerboardTexture = this.createCheckerboardTexture(1024, 2)
        checkerboardTexture.wrapS = RepeatWrapping
        checkerboardTexture.wrapT = RepeatWrapping
        checkerboardTexture.repeat.set(100, 100)
        const checkerboardMat = new MeshBasicMaterial({
            map: checkerboardTexture,
            opacity: 0.1,
            transparent: true
        })

        const plane = new PlaneGeometry(400, 400)

        this.ground = new Mesh(plane, checkerboardMat)
        this.ground.rotation.x = -Math.PI / 2
        this.ground.position.set(options?.x ?? 0, options?.y ?? 0.01, options?.z ?? 0)
        this.ground.receiveShadow = true
        this.scene.add(this.ground)

        const mirror = new Reflector(plane, {
            clipBias: 0.003,
            textureWidth: window.innerWidth * window.devicePixelRatio,
            textureHeight: window.innerHeight * window.devicePixelRatio,
            color: 0x00bfff
        })
        mirror.rotateX(-Math.PI / 2)
        this.scene.add(mirror)

        return this
    }

    public addOrbitControls = (minDistance: number, maxDistance: number, autoRotate = true) => {
        this.orbit = new OrbitControls(this.camera, this.renderer.domElement)
        this.orbit.minDistance = minDistance + (maxDistance - minDistance) / 2
        this.orbit.maxDistance = maxDistance
        this.orbit.autoRotate = autoRotate
        this.orbit.update()
        this.orbit.minDistance = minDistance
        return this
    }

    public addAmbientLight = (options: light) => {
        const ambientLight = new AmbientLight(options.color, options.intensity)
        this.scene.add(ambientLight)
        return this
    }

    public addDirectionalLight = (options: directionalLight) => {
        const directionalLight = new DirectionalLight(options.color, options.intensity)
        directionalLight.castShadow = true
        directionalLight.shadow.camera.top = 10
        directionalLight.shadow.camera.bottom = -10
        directionalLight.shadow.camera.right = 10
        directionalLight.shadow.camera.left = -10
        directionalLight.shadow.mapSize.set(4096, 4096)

        directionalLight.position.set(options.x ?? 0, options.y ?? 0, options.z ?? 0)
        this.scene.add(directionalLight)
        return this
    }

    private createCheckerboardTexture = (size: number, squares: number) => {
        const canvas = document.createElement('canvas')
        canvas.width = size
        canvas.height = size
        const context = canvas.getContext('2d')

        const squareSize = size / squares

        for (let y = 0; y < squares; y++) {
            for (let x = 0; x < squares; x++) {
                context!.fillStyle = (x + y) % 2 === 0 ? '#ffffff' : '#000000'
                context!.fillRect(x * squareSize, y * squareSize, squareSize, squareSize)
            }
        }

        const texture = new CanvasTexture(canvas)
        texture.wrapS = texture.wrapT = RepeatWrapping
        texture.anisotropy = 16
        return texture
    }

    public addFogExp2 = (color: ColorRepresentation, density?: number) => {
        this.scene.fog = new FogExp2(color, density)
        return this
    }

    public fillParent = () => {
        const parentElement = this.renderer.domElement.parentElement
        if (parentElement) {
            const width = parentElement.clientWidth
            const height = parentElement.clientHeight
            this.handleResize(width, height)
        }
        return this
    }

    public handleResize = (width = window.innerWidth, height = window.innerHeight) => {
        this.renderer.setSize(width, height)
        this.renderer.setPixelRatio(window.devicePixelRatio)
        this.camera.aspect = width / height
        this.camera.updateProjectionMatrix()
        return this
    }

    public addRenderCb = (callback: () => void) => {
        this.callback = callback
        return this
    }

    public startRenderLoop = () => {
        this.renderer.setAnimationLoop(() => {
            this.renderer.render(this.scene, this.camera)
            this.orbit.update()
            this.handleRobotShadow()
            if (this.callback) this.callback()
            if (!this.liveStreamTexture) return
        })
        return this
    }

    public stopRenderLoop = () => {
        this.renderer.setAnimationLoop(null)
        return this
    }

    public addArrowHelper = (options?: arrowOptions) => {
        const dir = new Vector3(
            options?.direction.x ?? 0,
            options?.direction.y ?? 0,
            options?.direction.z ?? 0
        )
        const origin = new Vector3(
            options?.origin.x ?? 0,
            options?.origin.y ?? 0,
            options?.origin.z ?? 0
        )
        const arrowHelper = new ArrowHelper(
            dir,
            origin,
            options?.length ?? 1.5,
            options?.color ?? 0xff0000
        )
        this.scene.add(arrowHelper)
        return this
    }

    public addModel = (model: URDFRobot) => {
        this.modelGroup = new Group()
        this.modelGroup.add(model)
        this.model = model
        this.scene.add(this.modelGroup)
        return this
    }

    public toggleFog = () => {
        this.scene.fog = this.scene.fog ? null : this.fog
    }

    private handleRobotShadow = () => {
        if (this.isLoaded) return
        const intervalId = setInterval(() => this.model?.traverse(c => (c.castShadow = true)), 10)
        setTimeout(() => clearInterval(intervalId), 1000)
        this.isLoaded = true
    }
}
