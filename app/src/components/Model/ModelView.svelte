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
	Camera
} from 'three';
import { XacroLoader } from 'xacro-parser';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import URDFLoader from 'urdf-loader';
import { servoBuffer } from '../../lib/socket'
import { lerp } from '../../lib/utils';
import uzip from 'uzip';
import { outControllerData } from '../../lib/store';
import Kinematic from '../../lib/kinematic';

let el: HTMLCanvasElement;
let scene: Scene, camera: Camera, renderer: WebGLRenderer, controls: OrbitControls, robot;

let modelAngles:number[] | Int8Array = new Array(12).fill(0)
let modelTargetAngles:number[] | Int8Array = new Array(12).fill(0)

let modelBodyAngles:EulerAngle = {omega: 0, phi: 0, psi: 0 }
let modelTargeBodyAngles:EulerAngle = {omega: 0, phi: 0, psi: 0 }

let modelBodyPoint:Point = {x: 0, y: 0, z: 0 }
let modelTargetBodyPoint:Point = {x: 0, y: 0, z: 0 }

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
    const center: number[] = [modelBodyPoint.x, modelBodyPoint.y, modelBodyPoint.z];
    const Lp = [[100,-100,100,1],[100,-100,-100,1],[-100,-100,100,1],[-100,-100,-100,1]]  

    const legs = kinematic.calcIK(Lp, angles, center)

    const dir = [ -1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1]

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
        modelTargeBodyAngles = {omega:(data[1]-128) / 3, phi:(data[2]-128) / 3, psi:(data[3]-128) / 4}
        calculateKinematics()
        //modelTargetBodyPoint = data.bodyPoint
    })

    servoBuffer.subscribe(angles => modelTargetAngles = angles)
});

const cacheModelFiles = async () => {
    const cacheKey = "files"
    const cache = await caches.open(cacheKey)

    let data = await fetch("/stl.zip").then(data => data.arrayBuffer())
    
    var files = uzip.parse(data);
    
    for(const [path, data] of Object.entries(files) as [path:string, data:Uint8Array][]){
        const url = new URL(path, window.location.href)
        cache.put(url, new Response(data));   
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

        scene.add( robot );

    },  (error) =>  console.log(error));
}

const createScene = () => {
    scene = new Scene();
    camera = new PerspectiveCamera();
    camera.position.set(-0.5, 0.5, 1);

    renderer = new WebGLRenderer({ antialias: true, canvas: el });
    renderer.outputEncoding = sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = PCFSoftShadowMap;
    document.body.appendChild(renderer.domElement);

    const directionalLight = new DirectionalLight(0xffffff, 1.0);
    directionalLight.castShadow = true;
    directionalLight.shadow.mapSize.setScalar(1024);
    directionalLight.position.set(5, 30, 5);
    scene.add(directionalLight);

    const ambientLight = new AmbientLight(0xffffff, 0.2);
    scene.add(ambientLight);

    const ground = new Mesh(new PlaneGeometry(), new ShadowMaterial({ opacity: 0.25 }));
    ground.rotation.x = -Math.PI / 2;
    ground.scale.setScalar(30);
    ground.receiveShadow = true;
    scene.add(ground);

    const size = 10;
    const divisions = 50;

    const gridHelper = new GridHelper(size, divisions);
    gridHelper.position.y = -0.24
    scene.add(gridHelper);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.minDistance = 0;
    controls.maxDistance = 4;
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

    for (let i = 0; i < servoNames.length; i++) {
        modelAngles[i] = lerp(robot.joints[servoNames[i]].angle * (180/Math.PI), modelTargetAngles[i], 0.1)
        robot.joints[servoNames[i]].setJointValue(MathUtils.degToRad(modelAngles[i]));
    }

    modelBodyAngles.omega = lerp(robot.rotation.x * (180/Math.PI), modelTargeBodyAngles.omega - 90, 0.1)
    modelBodyAngles.phi = lerp(robot.rotation.y * (180/Math.PI), modelTargeBodyAngles.phi, 0.1)
    modelBodyAngles.psi = lerp(robot.rotation.z * (180/Math.PI), modelTargeBodyAngles.psi + 90, 0.1)

    // robot.rotation.x =  MathUtils.degToRad(modelBodyAngles.omega)
    // robot.rotation.y = MathUtils.degToRad(modelBodyAngles.phi)
    // robot.rotation.z = MathUtils.degToRad(modelBodyAngles.psi)    
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
        {#each servoNames as name, i}
            <div class="flex justify-between mb-2">
                <span class="w-40">{name}: </span>
                <input type="range" min="-180" max="180" step="0.1" class="accent-primary" bind:value={$servoBuffer[i]}>
                <input class="w-24 bg-background" min="-180" max="180" step="0.1" bind:value={$servoBuffer[i]}> 
            </div>
        {/each}
    </div>

    <div>
    <h1 class="text-on-background text-xl mb-2">Body rotation</h1>
        {#each Object.entries(modelBodyAngles) as [name, angle]}
            <div class="flex justify-between mb-2">
                <span class="w-40">{name}: </span>
                <input type="range" min="-180" max="180" step="0.1" class="accent-primary" bind:value={modelTargeBodyAngles[name]} on:input={calculateKinematics}>
                <input class="w-24 bg-background" min="-180" max="180" step="0.1" bind:value={modelTargeBodyAngles[name]} on:input={calculateKinematics}> 
            </div>
        {/each}
    </div>

    <div>
        <h1 class="text-on-background text-xl mb-2">Body position</h1>
            {#each Object.entries(modelBodyPoint) as [name, coordinate]}
                <div class="flex justify-between mb-2">
                    <span class="w-40">{name}: </span>
                    <input type="range" min="-180" max="180" step="0.1" class="accent-primary" bind:value={modelTargetBodyPoint[name]} on:input={calculateKinematics}>
                    <input class="w-24 bg-background" min="-180" max="180" step="0.1" bind:value={modelTargetBodyPoint[name]} on:input={calculateKinematics}> 
                </div>
            {/each}
        </div>
</div>

<canvas bind:this={el} class="absolute"></canvas>