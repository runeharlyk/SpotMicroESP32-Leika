<script lang="ts">
import { onMount } from 'svelte';
import {
    WebGLRenderer,
    PerspectiveCamera,
    Scene,
    Mesh,
    PlaneGeometry,
    ShadowMaterial,
    DirectionalLight,
    PCFSoftShadowMap,
    sRGBEncoding,
    AmbientLight,
    MathUtils,
    LoaderUtils,
    GridHelper,
	Camera,
	FogExp2,
	MeshBasicMaterial,
	CanvasTexture,
	CircleGeometry,
	Object3D,
	type Event
} from 'three';
import { XacroLoader } from 'xacro-parser';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import URDFLoader from 'urdf-loader';
import { dataBuffer, servoBuffer } from '../../lib/socket'
import { lerp } from '../../lib/utils';
import uzip from 'uzip';
import { outControllerData } from '../../lib/store';
import Kinematic from '../../lib/kinematic';
import location from '../../lib/location';
import FileCache from '../../lib/cache';

let canvas: HTMLCanvasElement, streamCanvas: HTMLCanvasElement, stream: HTMLImageElement, scene: Scene, camera: Camera, renderer: WebGLRenderer, controls: OrbitControls, robot: Object3D<Event>, isLoaded = false;

let context: CanvasRenderingContext2D, texture: CanvasTexture

let modelAngles:number[] | Int16Array = new Array(12).fill(0)
let modelTargetAngles:number[] | Int16Array = new Array(12).fill(0)

let modelBodyAngles:EulerAngle = {omega: 0, phi: 0, psi: 0 }
let modelTargeBodyAngles:EulerAngle = {omega: 0, phi: 0, psi: 0 }

let modelBodyPoint:Point = {x: 0, y: 0, z: 0 }
let modelTargetBodyPoint:Point = {x: 0, y: 0, z: 0 }

const dir = [ -1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1]
const videoStream = `//${location}/api/stream`;

let showModel = true, showStream = false

const servoNames = [
    "front_left_shoulder", "front_left_leg", "front_left_foot", 
    "front_right_shoulder", "front_right_leg", "front_right_foot", 
    "rear_left_shoulder", "rear_left_leg", "rear_left_foot", 
    "rear_right_shoulder", "rear_right_leg", "rear_right_foot"
]

interface EulerAngle {
    omega: number;
    phi: number;
    psi: number;
}

interface Point {
    x: number;
    y: number;
    z: number;
}

interface BodyState {
    euler: EulerAngle;
    position: Point;
    legPositions:[number, number, number, number];
}

const radToDeg = (val:number) => val * (180 / Math.PI)
const degToRad = (val:number) => val * (Math.PI / 180)

const idle = () => {
    const angles = new Array(12).fill(0)
    servoBuffer.set(angles) 
}

const rest = () => {
    const angles = [0, 90, -180, 0, 90, -180, 0, 90, -180, 0, 90, -180, ]
    servoBuffer.set(angles) 
}

const stand = () => {
    const angles = [0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90]
    servoBuffer.set(angles) 
}

const calculateKinematics = () => {
    const kinematic = new Kinematic();
    const angles: number[] = [degToRad(modelTargeBodyAngles.omega), degToRad(modelTargeBodyAngles.phi), degToRad(modelTargeBodyAngles.psi)];
    const center: number[] = [modelTargetBodyPoint.x, modelTargetBodyPoint.y, modelTargetBodyPoint.z];
    const Lp = [[100,-100,100,1],[100,-100,-100,1],[-100,-100,100,1],[-100,-100,-100,1]]  

    const legs = kinematic.calcIK(Lp, angles, center)

    const legsAngles = legs
        .map(x => x.map(y => radToDeg(y)))
        .flat()
        .map((x, i) => x * dir[i])
    servoBuffer.set(legsAngles)
}

onMount(async () => {
    await cacheModelFiles()
    createScene()

    outControllerData.subscribe(data => {
        modelTargeBodyAngles = {omega:0, phi:(data[1]-128) / 3, psi:(data[2]-128) / 4}
        modelTargetBodyPoint = {x:(data[4]-128) / 2, y:data[5], z:(data[3]-128) / 2} // (data[5]-128) / 4
        calculateKinematics()
    })

    servoBuffer.subscribe(angles => modelTargetAngles = angles)

    modelTargeBodyAngles = {omega:0, phi:0, psi:0}
    modelTargetBodyPoint = {x:0, y:0, z:0}
    stand()
});

const cacheModelFiles = async () => {
    let data = await fetch("/stl.zip").then(data => data.arrayBuffer())
    
    var files = uzip.parse(data);
    await FileCache.openDatabase()
    
    for(const [path, data] of Object.entries(files) as [path:string, data:Uint8Array][]){
        const url = new URL(path, window.location.href)
        FileCache.saveFile(url.toString(), data)
    }
}

const loadModel = () => {
    const url = '/spot_micro.urdf.xacro';
    const xacroLoader = new XacroLoader();
    xacroLoader.load( url, xml => {
        const urdfLoader = new URDFLoader();
        urdfLoader.workingPath = LoaderUtils.extractUrlBase( url );

        robot = urdfLoader.parse( xml );
        robot.rotation.x = -Math.PI / 2;
        robot.rotation.z = Math.PI / 2;
        robot.traverse(c => c.castShadow = true);
        robot.updateMatrixWorld(true);
        robot.scale.setScalar(10);      

        scene.add( robot );

    },  (error) =>  console.log(error));
}

const createScene = () => {
    scene = new Scene();
    
    camera = new PerspectiveCamera();
    camera.position.set(-0.5, 0.5, 1);

    renderer = new WebGLRenderer({ antialias: true, canvas: canvas, alpha: true });
    renderer.outputEncoding = sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = PCFSoftShadowMap;
    document.body.appendChild(renderer.domElement);

    const directionalLight = new DirectionalLight(0xffffff, 0.9);
    directionalLight.castShadow = true;
    directionalLight.shadow.mapSize.setScalar(2048);
    directionalLight.shadow.mapSize.width = 1024;
    directionalLight.shadow.mapSize.height = 1024;
    directionalLight.position.set(50, 100, 100);
    directionalLight.shadow.radius = 5
    scene.add(directionalLight);

    const ambientLight = new AmbientLight(0xffffff, 0.3);
    scene.add(ambientLight);

    if(!showStream) scene.fog = new FogExp2( 0xcccccc, 0.015 );

    const ground = new Mesh( new PlaneGeometry(),  new ShadowMaterial({side: 2}));
    ground.rotation.x = -Math.PI / 2;
    ground.scale.setScalar(30);
    ground.position.y = -2
    ground.receiveShadow = true;
    scene.add(ground);

    context = streamCanvas.getContext("2d");
    texture = new CanvasTexture( stream );
    const liveStream = new Mesh( new CircleGeometry(35, 32), new MeshBasicMaterial({ map: texture }))
    liveStream.position.z = -50
    liveStream.visible = showStream
    scene.add(liveStream)

    const gridHelper = new GridHelper(250, 125);
    gridHelper.position.y = -2;
    scene.add(gridHelper);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.minDistance = 10;
    controls.maxDistance = 30;
    controls.update();

    loadModel()
    handleResize()
    render()
}

const handleResize = () => {
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(window.devicePixelRatio);

    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
}

const render = () => {
    requestAnimationFrame(render);
    renderer.render(scene, camera);

    if(!robot) return

    robot.rotation.z = lerp(robot.rotation.z, MathUtils.degToRad($dataBuffer[1]), 0.1)

    if(!isLoaded){
        const intervalId = setInterval(() => {
            robot.traverse(c => c.castShadow = true);
        }, 10);
        setTimeout(() => {
            clearInterval(intervalId)
        }, 1000);
        isLoaded = true;
    } 
    
    if(isLoaded && showStream) {
        context.drawImage(stream, 0, 0)
        texture.needsUpdate = true;
    }

    for (let i = 0; i < servoNames.length; i++) {
        modelAngles[i] = lerp(robot.joints[servoNames[i]].angle * (180/Math.PI), modelTargetAngles[i], 0.1)
        robot.joints[servoNames[i]].setJointValue(MathUtils.degToRad(modelAngles[i]));
    }

    modelBodyAngles.omega = lerp(robot.rotation.x * (180/Math.PI), modelTargeBodyAngles.omega - 90, 0.1)
    modelBodyAngles.phi = lerp(robot.rotation.y * (180/Math.PI), modelTargeBodyAngles.phi, 0.1)
    modelBodyAngles.psi = lerp(robot.rotation.z * (180/Math.PI), modelTargeBodyAngles.psi + 90, 0.1)  
}
</script>
  
<svelte:window on:resize={handleResize}></svelte:window>

<div class="absolute top-0 z-10 left-0 m-10">
    <h1 class="text-on-background text-xl mb-2">Poses</h1>
    <div class="flex gap-4">
        <button class="outline outline-primary p-2 rounded-md" on:click={idle}>Idle</button>
        <button class="outline outline-primary p-2 rounded-md" on:click={rest}>Rest</button>
        <button class="outline outline-primary p-2 rounded-md" on:click={stand}>Stand</button>
    </div>
    <div class="w-full">
    <h1 class="text-on-background text-xl mt-4">Motor angles</h1>
        {#each Object.entries(robot?.joints ?? {}).filter(x => x[1].jointValue.length > 0) as [name, joint], i}
            <div class="flex justify-between mb-2">
                <span class="w-40">{name}: </span>
                <input type="range" min="{radToDeg(joint.limit.lower)}" max="{radToDeg(joint.limit.upper)}" step="0.1" class="accent-primary" bind:value={$servoBuffer[i]}>
                <input class="w-24 bg-background" min="{radToDeg(joint.limit.lower)}" max="{radToDeg(joint.limit.upper)}" step="0.1" bind:value={$servoBuffer[i]}> 
            </div>
        {/each}
    </div>

    <div>
    <h1 class="text-on-background text-xl mb-2">Body rotation</h1>
        {#each Object.keys(modelBodyAngles) as name}
            <div class="flex justify-between mb-2">
                <span class="w-40">{name}: </span>
                <input type="range" min="-180" max="180" step="0.1" class="accent-primary" bind:value={modelTargeBodyAngles[name]} on:input={calculateKinematics}>
                <input class="w-24 bg-background" min="-180" max="180" step="0.1" bind:value={modelTargeBodyAngles[name]} on:input={calculateKinematics}> 
            </div>
        {/each}
    </div>

    <div>
        <h1 class="text-on-background text-xl mb-2">Body position</h1>
            {#each Object.keys(modelBodyPoint) as name}
                <div class="flex justify-between mb-2">
                    <span class="w-40">{name}: </span>
                    <input type="range" min="-180" max="180" step="0.1" class="accent-primary" bind:value={modelTargetBodyPoint[name]} on:input={calculateKinematics}>
                    <input class="w-24 bg-background" min="-180" max="180" step="0.1" bind:value={modelTargetBodyPoint[name]} on:input={calculateKinematics}> 
                </div>
            {/each}
        </div>
</div>

{#if showStream}
    <img
        bind:this={stream}
        src={videoStream}
        class="hidden"
        alt="Live stream is down"
        crossorigin="anonymous"
    />
    {/if}
<canvas bind:this={streamCanvas} class="hidden"></canvas>
<canvas bind:this={canvas} class="absolute"></canvas>