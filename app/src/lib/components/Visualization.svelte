<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { BufferGeometry, Line, LineBasicMaterial, Mesh, MeshBasicMaterial, Object3D, SphereGeometry, Vector3, type NormalBufferAttributes, type Object3DEventMap } from 'three';
	import uzip from 'uzip';
	import { ModesEnum, kinematicData, mode, model, outControllerData, servoAnglesOut } from '$lib/stores';
	import { footColor, isEmbeddedApp, throttler, toeWorldPositions } from '$lib/utilities';
	import { fileService } from '$lib/services';
	import { servoAngles, mpu, jointNames } from '$lib/stores';
	import SceneBuilder from '$lib/sceneBuilder';
	import { lerp, degToRad } from 'three/src/math/MathUtils';
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js';
	import Kinematic, { GaitPlanner, type body_state_t } from '$lib/kinematic';
	import { radToDeg } from 'three/src/math/MathUtils.js';
	import type { URDFRobot } from 'urdf-loader';
	import { get } from 'svelte/store';

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
    let Throttler = new throttler()

    let feet_trace = new Array(4).fill([]);
    let trace_lines: BufferGeometry<NormalBufferAttributes>[] = []
    let target: Object3D<Object3DEventMap>;

    let target_position = {x: 0, z: 0, yaw: 0}

    let kinematic = new Kinematic()
    let gaitPlanner = new GaitPlanner()
    const dir = [-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1]
    const Lp = [
        [100, -100, 100, 1],
        [100, -100, -100, 1],
        [-100, -100, 100, 1],
        [-100, -100, -100, 1],
    ];

    let settings = {
        'Internal kinematic':false,
        'Robot transform controls':false,
        'Auto orient robot':true,
        'Trace feet':debug,
        'Trace points': 30,
        'Fix camera on robot': true,
        'omega': 0,
        'phi': 0,
        'psi': 0,
        'x': 0,
        'y': 70,
        'z': 0
    }

	onMount(async () => {
        await cacheModelFiles()
        await createScene();
        if (!isEmbeddedApp && panel) createPanel();
        servoAngles.subscribe(updateAnglesFromStore)
        outControllerData.subscribe((buffer) => {
            if (!settings['Internal kinematic']) return

            const data = {
                stop: buffer[0],
                lx: buffer[1],
                ly: buffer[2],
                rx: buffer[3],
                ry: buffer[4],
                h: buffer[5],
                s: buffer[6],
            };

            settings.y = (data.h+128)*0.75

            switch (get(mode)) {
                case ModesEnum.Stand:
                    settings.omega = 0
                    settings.phi = data.rx / 4 
                    settings.psi = data.ry / 4 
                    settings.x = data.ly / 2 
                    settings.z = data.lx / 2
                    break;
            }
        })
	});
    
    const updateAnglesFromStore = (angles: number[]) => {
        if (sceneManager.isDragging) return
        if (settings['Internal kinematic']) return
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

        const general = gui_panel.addFolder('General');
        general.add(settings, 'Internal kinematic')
        general.add(settings, 'Robot transform controls')
        general.add(settings, 'Auto orient robot')

        const kinematic = gui_panel.addFolder('Kinematics');
        kinematic.add(settings, 'omega', -20, 20).onChange(updateKinematicPosition).listen();
        kinematic.add(settings, 'phi', -30, 30).onChange(updateKinematicPosition).listen();
        kinematic.add(settings, 'psi', -20, 15).onChange(updateKinematicPosition).listen();
        kinematic.add(settings, 'x', -90, 90).onChange(updateKinematicPosition)
        kinematic.add(settings, 'y', 0, 200).onChange(updateKinematicPosition)
        kinematic.add(settings, 'z', -130, 130).onChange(updateKinematicPosition)
 
        const visibility = gui_panel.addFolder('Visualization');
        visibility.add(settings, 'Trace feet')
        visibility.add(settings, 'Trace points', 1, 1000, 1)
    }

    const updateKinematicPosition = () => {
        kinematicData.set([
                settings.omega,
                settings.phi,
                settings.psi,
                settings.x,
                settings.y,
                settings.z
        ])
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
        Throttler.throttle(() => servoAnglesOut.set(modelTargetAngles.map(num => Math.round(num))), 100)
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
            .addTransformControls(sceneManager.model)
			.fillParent()
			.addRenderCb(render)
			.startRenderLoop();
            
            if (ground) sceneManager
			.addGroundPlane()
            .addGridHelper({ size: 30, divisions: 25 })
        
        
        const geometry = new SphereGeometry(0.1, 32, 16 ); 
        const material = new MeshBasicMaterial( { color: 0xffff00 } ); 
        target = new Mesh(geometry, material); 
        if (debug) {
            sceneManager.scene.add(target);
            sceneManager.addDragControl(updateAngles)
        }
        if (sky) sceneManager.addSky()

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

    const calculate_kinematics = () => {
        if (sceneManager.isDragging || !settings['Internal kinematic']) return
        const position:body_state_t = {
            omega: settings.omega,
            phi: settings.phi,
            psi: settings.psi,
            xm: settings.x,
            ym: settings.y,
            zm: settings.z,
            feet: Lp
        }

        let new_angles = kinematic.calcIK(position).map((x, i) => radToDeg(x * dir[i]));
        modelTargetAngles = new_angles;
    }

    const orient_robot = (robot: URDFRobot, toes:Vector3[]) => {
        if (settings['Robot transform controls'] || !settings['Auto orient robot']) return
        robot.position.y = robot.position.y - Math.min(...toes.map(toe => toe.y));
        robot.position.z = lerp(robot.position.z, -settings.x / 100, 0.1);

        robot.rotation.z = lerp(robot.rotation.z, degToRad(settings.phi + $mpu.heading + 90), 0.1);
        robot.rotation.y = lerp(robot.rotation.y, degToRad(settings.omega - settings.z / 2.5), 0.1);
        robot.rotation.x = lerp(robot.rotation.x, degToRad(settings.psi - 90), 0.1);
    }

    const update_camera = (robot:URDFRobot) => {
        if (!settings['Fix camera on robot']) return
        sceneManager.orbit.target = robot.position.clone()
    }

    const update_gate = () => {
        if (get(mode) != ModesEnum.Walk) return
        const body_state = {
            omega: settings.omega,
            phi: settings.phi,
            psi: settings.psi,
            xm: settings.x,
            ym: settings.y,
            zm: settings.z,
            feet: Lp
        }
        gaitPlanner.step(body_state, 0.1)

    }

    const update_robot_position = (robot:URDFRobot) => {
        if (!settings['Robot transform controls']) return
        settings.omega = radToDeg(robot.rotation.y)
        settings.phi = radToDeg(robot.rotation.z) + $mpu.heading -90
        settings.psi = radToDeg(robot.rotation.x) + 90
        settings.x = robot.position.z * 100
        settings.z = -robot.position.x * 100
    }

    const updateTargetPosition = () => {
        target.position.x = lerp(target.position.x, target_position.x, 0.5)
        target.position.z = lerp(target.position.z, target_position.z, 0.5)
    }

	const render = () => {
		const robot = sceneManager.model;
		if (!robot) return;

        const toes = toeWorldPositions(robot)

        renderTraceLines(toes)
        update_camera(robot)
        update_gate()
        calculate_kinematics()
        update_robot_position(robot)
        
        sceneManager.transformControl.showX = settings['Robot transform controls']
        sceneManager.transformControl.showY = settings['Robot transform controls']
        sceneManager.transformControl.showZ = settings['Robot transform controls']
        
		for (let i = 0; i < $jointNames.length; i++) {
            currentModelAngles[i] = lerp(
                (robot.joints[$jointNames[i]].angle as number) * (180 / Math.PI),
				modelTargetAngles[i],
				0.1
                );
                robot.joints[$jointNames[i]].setJointValue(degToRad(currentModelAngles[i]));
        }

        orient_robot(robot, toes) 
        updateTargetPosition();
	};


</script>

<svelte:window on:resize={sceneManager.fillParent} />

<canvas bind:this={canvas}></canvas>
