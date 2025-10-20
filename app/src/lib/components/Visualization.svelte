<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import {
        Mesh,
        MeshBasicMaterial,
        type Object3D,
        SphereGeometry,
        Vector3,
        type Object3DEventMap,
        Color
    } from 'three'
    import {
        ModesEnum,
        kinematicData,
        mode,
        model,
        outControllerData,
        servoAnglesOut,
        servoAngles,
        mpu,
        jointNames,
        currentKinematic,
        walkGait,
        walkGaitToMode
    } from '$lib/stores'
    import { populateModelCache, throttler, getToeWorldPositions } from '$lib/utilities'
    import SceneBuilder from '$lib/sceneBuilder'
    import { lerp, degToRad } from 'three/src/math/MathUtils'
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js'
    import { type body_state_t } from '$lib/kinematic'
    import { BezierState, CalibrationState, IdleState, RestState, StandState } from '$lib/gait'
    import { radToDeg } from 'three/src/math/MathUtils.js'
    import type { URDFRobot } from 'urdf-loader'
    import { get } from 'svelte/store'

    interface Props {
        defaultColor?: string | null
        orbit?: boolean
        panel?: boolean
        debug?: boolean
        ground?: boolean
    }

    let {
        defaultColor = '#0091ff',
        orbit = false,
        panel = true,
        debug = false,
        ground = true
    }: Props = $props()

    let sceneManager = $state(new SceneBuilder())
    let canvas: HTMLCanvasElement

    let currentModelAngles: number[] = new Array(12).fill(0)
    let modelTargetAngles: number[] = new Array(12).fill(0)
    let gui_panel: GUI
    let Throttler = new throttler()

    let target: Object3D<Object3DEventMap>

    let target_position = { x: 0, z: 0, yaw: 0 }

    let kinematic = get(currentKinematic)

    let planners = {
        [ModesEnum.Deactivated]: new IdleState(),
        [ModesEnum.Idle]: new IdleState(),
        [ModesEnum.Calibration]: new CalibrationState(),
        [ModesEnum.Rest]: new RestState(),
        [ModesEnum.Stand]: new StandState(),
        [ModesEnum.Walk]: new BezierState()
    }
    let lastTick = performance.now()

    const dir = [1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1]

    let body_state = {
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: 0.5,
        zm: 0,
        feet: kinematic.getDefaultFeetPos(),
        cumulative_x: 0,
        cumulative_y: 0,
        cumulative_z: 0,
        cumulative_roll: 0,
        cumulative_pitch: 0,
        cumulative_yaw: 0
    }

    let settings = {
        'Internal kinematic': true,
        'Robot transform controls': false,
        'Auto orient robot': true,
        'Trace feet': debug,
        'Target position': false,
        'Trace points': 30,
        'Fix camera on robot': true,
        'Smooth motion': true,
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: 0.7,
        zm: 0,
        Background: defaultColor
    }

    onMount(async () => {
        await populateModelCache()
        await createScene()
        servoAngles.subscribe(updateAnglesFromStore)
        walkGait.subscribe(gait => planners[ModesEnum.Walk].set_mode(walkGaitToMode(gait)))
        if (panel) createPanel()
    })

    onDestroy(() => {
        canvas.remove()
        gui_panel?.destroy()
    })

    const updateAnglesFromStore = (angles: number[]) => {
        if (sceneManager.isDragging) return
        if (settings['Internal kinematic']) return
        modelTargetAngles = angles
    }

    const createPanel = () => {
        gui_panel = new GUI({ width: 310 })
        gui_panel.close()
        gui_panel.domElement.id = 'three-gui-panel'

        const general = gui_panel.addFolder('General')
        general.add(settings, 'Internal kinematic')
        general.add(settings, 'Robot transform controls')
        general.add(settings, 'Auto orient robot')

        const kinematic = gui_panel.addFolder('Kinematics')
        kinematic.add(settings, 'omega', -20, 20).onChange(updateKinematicPosition).listen()
        kinematic.add(settings, 'phi', -30, 30).onChange(updateKinematicPosition).listen()
        kinematic.add(settings, 'psi', -20, 15).onChange(updateKinematicPosition).listen()
        kinematic.add(settings, 'xm', -1, 1).onChange(updateKinematicPosition).listen()
        kinematic.add(settings, 'ym', 0, 1).onChange(updateKinematicPosition).listen()
        kinematic.add(settings, 'zm', -1.3, 1.3).onChange(updateKinematicPosition).listen()

        const visibility = gui_panel.addFolder('Visualization')
        visibility.add(settings, 'Trace feet')
        visibility.add(settings, 'Trace points', 1, 1000, 1)
        visibility.add(settings, 'Target position')
        visibility.add(settings, 'Smooth motion')
        visibility.addColor(settings, 'Background').onChange(setSceneBackground).listen()
    }

    const updateKinematicPosition = () => {
        kinematicData.set([
            settings.omega,
            settings.phi,
            settings.psi,
            settings.xm,
            settings.ym,
            settings.zm
        ])
    }

    const setSceneBackground = (c: string | null) => (sceneManager.scene.background = new Color(c!))

    const updateAngles = (name: string, angle: number) => {
        modelTargetAngles[$jointNames.indexOf(name)] = angle * (180 / Math.PI)
        Throttler.throttle(
            () => servoAnglesOut.set(modelTargetAngles.map(num => Math.round(num))),
            100
        )
    }

    const createScene = async () => {
        sceneManager
            .addRenderer({ antialias: true, canvas, alpha: true })
            .addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
            .addOrbitControls(2, 20, orbit)
            .addDirectionalLight({ x: 10, y: 20, z: 10, color: 0xffffff, intensity: 3 })
            .addAmbientLight({ color: 0xffffff, intensity: 0.5 })
            .addFogExp2(0xcccccc, 0.015)
            .addModel($model as URDFRobot)
            .addTransformControls(sceneManager.model)
            .fillParent()
            .addRenderCb(render)
            .startRenderLoop()

        if (ground) sceneManager.addGroundPlane()

        const geometry = new SphereGeometry(0.1, 32, 16)
        const material = new MeshBasicMaterial({ color: 0xffff00 })
        target = new Mesh(geometry, material)
        sceneManager.scene.add(target)

        if (debug) {
            sceneManager.addDragControl((angles: Record<string, number>) => {
                Object.entries(angles).forEach(([name, angle]) => {
                    updateAngles(name, angle)
                })
            })
        }
        if (defaultColor) setSceneBackground(settings['Background'] || defaultColor)
    }

    const calculate_kinematics = () => {
        if (sceneManager.isDragging || !settings['Internal kinematic']) return
        const position: body_state_t = {
            omega: settings.omega,
            phi: settings.phi,
            psi: settings.psi,
            xm: settings.xm,
            ym: settings.ym,
            zm: settings.zm,
            feet: body_state.feet,
            cumulative_x: body_state.cumulative_x,
            cumulative_y: body_state.cumulative_y,
            cumulative_z: body_state.cumulative_z,
            cumulative_roll: body_state.cumulative_roll,
            cumulative_pitch: body_state.cumulative_pitch,
            cumulative_yaw: body_state.cumulative_yaw
        }

        let new_angles = kinematic.calcIK(position).map((x, i) => radToDeg(x * dir[i]))
        modelTargetAngles = new_angles
    }

    const orient_robot = (robot: URDFRobot, toes: Vector3[]) => {
        if (settings['Robot transform controls'] || !settings['Auto orient robot']) return
        robot.position.y = robot.position.y - Math.min(...toes.map(toe => toe.y))

        const cumulativeYaw = body_state.cumulative_yaw

        const cosYaw = Math.cos(cumulativeYaw)
        const sinYaw = Math.sin(cumulativeYaw)
        const rotatedXm = settings.xm * cosYaw - settings.zm * sinYaw
        const rotatedZm = settings.xm * sinYaw + settings.zm * cosYaw

        robot.position.x = smooth(robot.position.x, -rotatedZm - body_state.cumulative_z * 1.2, 0.1)
        robot.position.z = smooth(robot.position.z, -rotatedXm - body_state.cumulative_x * 1.2, 0.1)

        const pitch = degToRad(settings.psi - 90) + body_state.cumulative_pitch
        const roll = degToRad(settings.omega) + body_state.cumulative_roll

        robot.rotation.z = smooth(
            robot.rotation.z,
            degToRad(-settings.phi + $mpu.heading + 90) + cumulativeYaw,
            0.1
        )
        robot.rotation.y = smooth(robot.rotation.y, roll, 0.1)
        robot.rotation.x = smooth(robot.rotation.x, pitch, 0.1)
    }

    const update_camera = (robot: URDFRobot) => {
        if (!settings['Fix camera on robot']) return
        sceneManager.orbit.target = robot.position.clone()
    }

    const smooth = (start: number, end: number, amount: number) => {
        return settings['Smooth motion'] ? lerp(start, end, amount) : end
    }

    const update_gait = () => {
        if (sceneManager.isDragging || !settings['Internal kinematic']) return
        const controlData = get(outControllerData)
        const data = {
            lx: controlData[0],
            ly: controlData[1],
            rx: controlData[2],
            ry: controlData[3],
            h: controlData[4],
            s: controlData[5],
            s1: controlData[6]
        }
        body_state.ym = data.h

        let planner = planners[get(mode)]
        const delta = performance.now() - lastTick
        lastTick = performance.now()

        body_state = planner.step(body_state, data, delta)

        settings.omega = body_state.omega
        settings.phi = body_state.phi
        settings.psi = body_state.psi
        settings.xm = body_state.xm
        settings.ym = body_state.ym
        settings.zm = body_state.zm
    }

    const update_robot_position = (robot: URDFRobot) => {
        if (!settings['Robot transform controls']) return
        settings.omega = radToDeg(robot.rotation.y)
        settings.phi = radToDeg(robot.rotation.z) + $mpu.heading - 90
        settings.psi = radToDeg(robot.rotation.x) + 90
        settings.xm = robot.position.z * 100
        settings.zm = -robot.position.x * 100
    }

    const updateTargetPosition = () => {
        target.visible = settings['Target position']
        target.position.x = smooth(target.position.x, target_position.x, 0.5)
        target.position.z = smooth(target.position.z, target_position.z, 0.5)
    }

    const render = () => {
        const robot = sceneManager.model
        if (!robot) return

        const toes = getToeWorldPositions(robot)

        update_camera(robot)
        update_gait()
        calculate_kinematics()
        update_robot_position(robot)

        sceneManager.transformControl.showX = settings['Robot transform controls']
        sceneManager.transformControl.showY = settings['Robot transform controls']
        sceneManager.transformControl.showZ = settings['Robot transform controls']

        for (let i = 0; i < $jointNames.length; i++) {
            currentModelAngles[i] = smooth(
                (robot.joints[$jointNames[i]].angle as number) * (180 / Math.PI),
                modelTargetAngles[i],
                0.1
            )
            robot.joints[$jointNames[i]].setJointValue(degToRad(currentModelAngles[i]))
        }

        orient_robot(robot, toes)
        updateTargetPosition()
    }
</script>

<svelte:window onresize={sceneManager.fillParent} />

<canvas bind:this={canvas}></canvas>
