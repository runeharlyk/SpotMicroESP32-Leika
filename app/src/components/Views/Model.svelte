<script lang="ts">
import { onDestroy, onMount } from 'svelte';
import { CanvasTexture, CircleGeometry, Mesh, MeshBasicMaterial} from 'three';
import {socket, angles, mpu } from '$lib/socket'
import { lerp } from '$lib/utilities';
import uzip from 'uzip';
import { model, outControllerData } from '$lib/store';
import { ForwardKinematics } from '$lib/kinematic';
import location from '$lib/location';
import FileService from '$lib/services/file-service';
import SceneBuilder from '$lib/sceneBuilder';

let sceneManager:SceneBuilder
let canvas: HTMLCanvasElement, streamCanvas: HTMLCanvasElement, stream: HTMLImageElement
let context: CanvasRenderingContext2D, texture: CanvasTexture

let modelAngles:number[] | Int16Array = new Array(12).fill(0)
let modelTargetAngles:number[] | Int16Array = new Array(12).fill(0)

let modelBodyAngles:EulerAngle = { omega: 0, phi: 0, psi: 0 }
let modelTargeBodyAngles:EulerAngle = { omega: 0, phi: 0, psi: 0 }

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

const degToRad = (val:number) => val * (Math.PI / 180)

onMount(async () => {
    await cacheModelFiles()
    await createScene()

    outControllerData.subscribe(data => {        
        $socket.send(JSON.stringify({
            type: "kinematic/bodystate", 
            angles:[0, (data[1]-128)/3, (data[2]-128) / 4], 
            position:[(data[4]-128)/2, data[5], (data[3]-128)/2]}))
    })
});

onDestroy(() => {
    canvas.remove()
})

const cacheModelFiles = async () => {
    let data = await fetch("/stl.zip").then(data => data.arrayBuffer())
    
    var files = uzip.parse(data);
    await FileService.openDatabase()
    
    for(const [path, data] of Object.entries(files) as [path:string, data:Uint8Array][]){
        const url = new URL(path, window.location.href)
        FileService.saveFile(url.toString(), data)
    }
}

const updateAngles = (name:string, angle:number) => {
    modelTargetAngles[servoNames.indexOf(name)] = angle * (180/Math.PI)
    $socket.send(JSON.stringify({type:"kinematic/angle", angle:angle * (180/Math.PI), id:servoNames.indexOf(name)}))
}

const createScene = async () => {
    sceneManager = new SceneBuilder()
        .addRenderer({ antialias: true, canvas: canvas, alpha: true})
        .addPerspectiveCamera({x:-0.5, y:0.5, z:1})
        .addOrbitControls(10, 30)
        .addSky()
        .addGroundPlane({x:0, y:-2, z:0})
        .addGridHelper({size:250, divisions:125, y:-2})
        .addAmbientLight({color:0xffffff, intensity:0.7})
        .addDirectionalLight({x:10, y:100, z:10, color:0xffffff, intensity:1})
        .addArrowHelper({origin:{x:0, y:0, z:0}, direction:{x:0, y:-2, z:0}})
        .addFogExp2(0xcccccc, 0.015)
        .addModel($model)
        .addDragControl(updateAngles) 
        .handleResize()
        .addRenderCb(render)
        .startRenderLoop()

    addVideoStream()
}

const addVideoStream = () => {
    context = streamCanvas.getContext("2d");
    texture = new CanvasTexture( stream );
    const liveStream = new Mesh( new CircleGeometry(35, 32), new MeshBasicMaterial({ map: texture }))
    liveStream.position.z = -50
    liveStream.visible = showStream
    sceneManager.scene.add(liveStream)
}

const handleVideoStream = () => {
    if(!showStream) return
    context.drawImage(stream, 0, 0)
    texture.needsUpdate = true;
}

const render = () => {
    const robot = sceneManager.model
    if(!robot) return

    const forwardKinematics = new ForwardKinematics()

    const points = forwardKinematics.calculateFootpoints(modelAngles.map(ang => degToRad(ang)) as number[])
    robot.position.y = Math.max(...points.map(coord => coord[0] / 100)) - 2.7
    robot.rotation.z = lerp(robot.rotation.z, degToRad($mpu.heading + 90), 0.1)
    modelTargetAngles = $angles 
   
    handleVideoStream()

    for (let i = 0; i < servoNames.length; i++) {
        modelAngles[i] = lerp(robot.joints[servoNames[i]].angle * (180/Math.PI), modelTargetAngles[i], 0.1)
        robot.joints[servoNames[i]].setJointValue(degToRad(modelAngles[i]));
    }

    modelBodyAngles.omega = lerp(robot.rotation.x * (180/Math.PI), modelTargeBodyAngles.omega - 90, 0.1)
    modelBodyAngles.phi = lerp(robot.rotation.y * (180/Math.PI), modelTargeBodyAngles.phi, 0.1)
    modelBodyAngles.psi = lerp(robot.rotation.z * (180/Math.PI), modelTargeBodyAngles.psi + 90, 0.1)  
}
</script>
  
<svelte:window on:resize={sceneManager.handleResize}></svelte:window>

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