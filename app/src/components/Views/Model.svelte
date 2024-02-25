<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { BufferGeometry, CanvasTexture, CircleGeometry, CubicBezierCurve3, Line, LineBasicMaterial, Mesh, MeshBasicMaterial, Vector3, type NormalBufferAttributes } from 'three';
	import socketService from '$lib/services/socket-service';
	import uzip from 'uzip';
	import { model } from '$lib/stores';
	import { footColor, isEmbeddedApp, location, toeWorldPositions } from '$lib/utilities';
	import { fileService } from '$lib/services';
	import { servoAngles, mpu, jointNames } from '$lib/stores';
	import SceneBuilder from '$lib/sceneBuilder';
	import { lerp, degToRad } from 'three/src/math/MathUtils';
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js';

	let sceneManager = new SceneBuilder();
	let canvas: HTMLCanvasElement, streamCanvas: HTMLCanvasElement, stream: HTMLImageElement;
	let context: CanvasRenderingContext2D, texture: CanvasTexture;

	let modelAngles: number[] | Int16Array = new Array(12).fill(0);
	let modelTargetAngles: number[] | Int16Array = new Array(12).fill(0);

    let feet_trace = new Array(4).fill([]);
    let trace_lines: BufferGeometry<NormalBufferAttributes>[] = []

	const videoStream = `//${location}/api/stream`;

	let showStream = false;

    let settings = {
        'Trace feet':true,
        'Trace points': 30
    }

	onMount(async () => {
        await cacheModelFiles()
        await createScene();
        if (!isEmbeddedApp) createPanel();
	});

	onDestroy(() => {
		canvas.remove();
	});

    const createPanel = () => {
        const panel = new GUI({width: 310});
        panel.close();
        panel.domElement.id = 'three-gui-panel';
 
        const visibility = panel.addFolder('Visualization');
        visibility.add(settings, 'Trace feet')
        visibility.add(settings, 'Trace points', 1, 1000, 1)
    }

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
		sceneManager
			.addRenderer({ antialias: true, canvas: canvas, alpha: true })
			.addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
			.addOrbitControls(8, 30)
			.addSky()
			.addGroundPlane()
			.addGridHelper({ size: 250, divisions: 125 })
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

		for (let i = 0; i < 4; i++) {
			const geometry = new BufferGeometry();
			const material = new LineBasicMaterial({ color: footColor() });
			const line = new Line(geometry, material);
			trace_lines.push(geometry);
			sceneManager.scene.add(line);
		}
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

    const renderTraceLines = (foot_positions: Vector3[]) => {
        if (!settings['Trace feet']) {
            if (!feet_trace.length) return 
            trace_lines.forEach((line, i) => line.setFromPoints(feet_trace[i].slice(-1)))
            feet_trace = new Array(4).fill([])
            return
        }
        
        trace_lines.forEach((line, i) => {
            feet_trace[i].push(foot_positions[i])
            feet_trace[i] = feet_trace[i].slice(-settings['Trace points'])
            line.setFromPoints(feet_trace[i]);
        })
    }

	const render = () => {
		const robot = sceneManager.model;
		if (!robot) return;

        const toes = toeWorldPositions(robot)

        renderTraceLines(toes)

		robot.position.y = robot.position.y - Math.min(...toes.map(toe => toe.y));
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
