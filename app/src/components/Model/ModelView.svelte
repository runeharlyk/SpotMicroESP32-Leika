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
    LoaderUtils
} from 'three';
import { XacroLoader } from 'xacro-parser';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import URDFLoader from 'urdf-loader';
import { servoBuffer } from '../../lib/socket'
import { lerp } from '../../lib/utils';
import uzip from 'uzip';
import { outControllerData } from '../../lib/store';

let el: HTMLCanvasElement;
let scene, camera, renderer, robot, controls;
let angles = new Int8Array(12)

const servoNames = [
    "front_left_shoulder", "front_left_leg", "front_left_foot", 
    "front_right_shoulder", "front_right_leg", "front_right_foot", 
    "rear_left_shoulder", "rear_left_leg", "rear_left_foot", 
    "rear_right_shoulder", "rear_right_leg", "rear_right_foot"]

onMount(async () => {
    await cacheModelFiles()
    createScene()
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
        robot.rotation.x = Math.PI / 0.6666;
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
    
    const newAngles = $servoBuffer
    if(JSON.stringify(angles) === JSON.stringify(newAngles)) return

    for (let i = 0; i < servoNames.length; i++) {
        angles[i] = lerp(robot.joints[servoNames[i]].angle * (180/Math.PI), newAngles[i], 0.2)
        robot.joints[servoNames[i]].setJointValue(MathUtils.degToRad(angles[i]));
    }
}
</script>
  
<svelte:window on:resize={handleResize}></svelte:window>

<canvas bind:this={el} class="absolute"></canvas>