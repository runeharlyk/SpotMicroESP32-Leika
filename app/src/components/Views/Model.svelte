<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { CanvasTexture, CircleGeometry, Mesh, MeshBasicMaterial } from 'three';
	import socketService from '$lib/services/socket-service';
	import uzip from 'uzip';
	import { model } from '$lib/stores';
	import { ForwardKinematics } from '$lib/kinematic';
	import { location } from '$lib/utilities';
	import { fileService } from '$lib/services';
	import { servoAngles, mpu, jointNames } from '$lib/stores';
	import SceneBuilder from '$lib/sceneBuilder';
	import { lerp, degToRad } from 'three/src/math/MathUtils';

	let sceneManager: SceneBuilder;
	let canvas: HTMLCanvasElement, streamCanvas: HTMLCanvasElement, stream: HTMLImageElement;
	let context: CanvasRenderingContext2D, texture: CanvasTexture;

	let modelAngles: number[] | Int16Array = new Array(12).fill(0);
	let modelTargetAngles: number[] | Int16Array = new Array(12).fill(0);

	const videoStream = `//${location}/api/stream`;

	let showStream = false;

	onMount(async () => {
		await cacheModelFiles();
		await createScene();
	});

	onDestroy(() => {
		canvas.remove();
	});

	const cacheModelFiles = async () => {
		let data = await fetch('/stl.zip').then((data) => data.arrayBuffer());

		var files = uzip.parse(data);

		for (const [path, data] of Object.entries(files) as [path: string, data: Uint8Array][]) {
			const url = new URL(path, window.location.href);
			fileService.saveFile(url.toString(), data);
		}
	};

	const updateAngles = (name: string, angle: number) => {
		modelTargetAngles[$jointNames.indexOf(name)] = angle * (180 / Math.PI);
		socketService.send(
			JSON.stringify({
				type: 'kinematic/angle',
				angle: angle * (180 / Math.PI),
				id: $jointNames.indexOf(name)
			})
		);
	};

	const createScene = async () => {
		sceneManager = new SceneBuilder()
			.addRenderer({ antialias: true, canvas: canvas, alpha: true })
			.addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
			.addOrbitControls(10, 30)
			.addSky()
			.addGroundPlane({ x: 0, y: -2, z: 0 })
			.addGridHelper({ size: 250, divisions: 125, y: -2 })
			.addAmbientLight({ color: 0xffffff, intensity: 0.7 })
			.addDirectionalLight({ x: 10, y: 100, z: 10, color: 0xffffff, intensity: 1 })
			.addArrowHelper({ origin: { x: 0, y: 0, z: 0 }, direction: { x: 0, y: -2, z: 0 } })
			.addFogExp2(0xcccccc, 0.015)
			.addModel($model)
			.addDragControl(updateAngles)
			.handleResize()
			.addRenderCb(render)
			.startRenderLoop();

		addVideoStream();
	};

	const addVideoStream = () => {
		context = streamCanvas.getContext('2d')!;
		texture = new CanvasTexture(stream);
		const liveStream = new Mesh(
			new CircleGeometry(35, 32),
			new MeshBasicMaterial({ map: texture })
		);
		liveStream.position.z = -50;
		liveStream.visible = showStream;
		sceneManager.scene.add(liveStream);
	};

	const handleVideoStream = () => {
		if (!showStream) return;
		context.drawImage(stream, 0, 0);
		texture.needsUpdate = true;
	};

	const render = () => {
		const robot = sceneManager.model;
		if (!robot) return;

		const forwardKinematics = new ForwardKinematics();

		const points = forwardKinematics.calculateFootpoints(
			modelAngles.map((ang) => degToRad(ang)) as number[]
		);
		robot.position.y = Math.max(...points.map((coord) => coord[0] / 100)) - 2.7;
		robot.rotation.z = lerp(robot.rotation.z, degToRad($mpu.heading + 90), 0.1);
		modelTargetAngles = $servoAngles;

		handleVideoStream();

		for (let i = 0; i < $jointNames.length; i++) {
			modelAngles[i] = lerp(
				(robot.joints[$jointNames[i]].angle as number) * (180 / Math.PI),
				modelTargetAngles[i],
				0.1
			);
			robot.joints[$jointNames[i]].setJointValue(degToRad(modelAngles[i]));
		}
	};
</script>

<svelte:window on:resize={sceneManager.handleResize} />

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
