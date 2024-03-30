<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { BufferGeometry, Line, LineBasicMaterial, Vector3, type NormalBufferAttributes } from 'three';
	import uzip from 'uzip';
	import { model, servoAnglesOut } from '$lib/stores';
	import { footColor, isEmbeddedApp, toeWorldPositions } from '$lib/utilities';
	import { fileService } from '$lib/services';
	import { servoAngles, mpu, jointNames } from '$lib/stores';
	import SceneBuilder from '$lib/sceneBuilder';
	import { lerp, degToRad } from 'three/src/math/MathUtils';
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js';

    export let sky = true
    export let orbit = false
    export let panel = true
    export let debug = false
    export let ground = true

	let sceneManager = new SceneBuilder();
	let canvas: HTMLCanvasElement

	let currentModelAngles: number[] = new Array(12).fill(0);
	let modelTargetAngles: number[] = new Array(12).fill(0)
    let gui_panel: GUI

    let feet_trace = new Array(4).fill([]);
    let trace_lines: BufferGeometry<NormalBufferAttributes>[] = []

    let settings = {
        'Trace feet':debug,
        'Trace points': 30,
        'Fix camera on robot': true
    }

	onMount(async () => {
        await cacheModelFiles()
        await createScene();
        if (!isEmbeddedApp && panel) createPanel();
        servoAngles.subscribe(updateAnglesFromStore)
	});
    
    const updateAnglesFromStore = (angles: number[]) => {
        if (sceneManager.isDragging) return
        modelTargetAngles = angles;
    }

	onDestroy(() => {
        canvas.remove()
        gui_panel?.destroy()
    });

    const createPanel = () => {
        gui_panel = new GUI({width: 310});
        gui_panel.close();
        gui_panel.domElement.id = 'three-gui-panel';
 
        const visibility = gui_panel.addFolder('Visualization');
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
        servoAnglesOut.set(modelTargetAngles)
	};

	const createScene = async () => {
		sceneManager
			.addRenderer({ antialias: true, canvas, alpha: true })
			.addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
			.addOrbitControls(8, 30, orbit)
            .addDirectionalLight({ x: 10, y: 20, z: 10, color: 0xffffff, intensity: 0.9 })
			.addAmbientLight({ color: 0xffffff, intensity: 0.6 })
			.addFogExp2(0xcccccc, 0.015)
			.addModel($model)
			.fillParent()
			.addRenderCb(render)
			.startRenderLoop();
            
            if (ground) sceneManager
			.addGroundPlane()
            .addGridHelper({ size: 30, divisions: 25 })
        
        
        if (sky) sceneManager.addSky()
        if (debug) sceneManager.addDragControl(updateAngles)

        for (let i = 0; i < 4; i++) {
			const geometry = new BufferGeometry();
			const material = new LineBasicMaterial({ color: footColor() });
			const line = new Line(geometry, material);
			trace_lines.push(geometry);
			sceneManager.scene.add(line);
		}
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

        if (settings['Fix camera on robot']) {
            sceneManager.controls.target = robot.position.clone()
        }

		robot.position.y = robot.position.y - Math.min(...toes.map(toe => toe.y));
		robot.rotation.z = lerp(robot.rotation.z, degToRad($mpu.heading + 90), 0.1);

		for (let i = 0; i < $jointNames.length; i++) {
			currentModelAngles[i] = lerp(
				(robot.joints[$jointNames[i]].angle as number) * (180 / Math.PI),
				modelTargetAngles[i],
				0.1
			);
			robot.joints[$jointNames[i]].setJointValue(degToRad(currentModelAngles[i]));
		}
	};


</script>

<svelte:window on:resize={sceneManager.fillParent} />

<canvas bind:this={canvas}></canvas>
