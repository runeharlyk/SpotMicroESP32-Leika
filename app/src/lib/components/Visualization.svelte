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
        mode,
        model,
        input,
        servoAnglesOut,
        servoAngles,
        mpu,
        jointNames,
        currentKinematic,
        walkGait,
        kinematicData
    } from '$lib/stores'
    import { populateModelCache, getToeWorldPositions } from '$lib/utilities'
    import SceneBuilder from '$lib/sceneBuilder'
    import { lerp, degToRad } from 'three/src/math/MathUtils'
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js'
    import { type body_state_t } from '$lib/kinematic'
    import {
        BezierState,
        CalibrationState,
        GaitState,
        IdleState,
        RestState,
        StandState
    } from '$lib/gait'
    import { radToDeg } from 'three/src/math/MathUtils.js'
    import type { URDFRobot } from 'urdf-loader'
    import { get } from 'svelte/store'
    import { AnglesData, KinematicData, ModesEnum } from '$lib/platform_shared/websocket_message'

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
    const NUM_ANGLES = 12 // TODO: This number should come from the robot

    let currentModelAngles: AnglesData = AnglesData.create({
        angles: new Array(NUM_ANGLES).fill(0)
    })
    let modelTargetAngles: AnglesData = AnglesData.create({ angles: new Array(NUM_ANGLES).fill(0) })
    let gui_panel: GUI
    const SMOOTH_AMOUNT = 0.2

    let target: Object3D<Object3DEventMap>

    let target_position = { x: 0, z: 0, yaw: 0 }

    let kinematic = get(currentKinematic)

    const planners: Record<ModesEnum, GaitState> = {
        [ModesEnum.DEACTIVATED]: new IdleState(),
        [ModesEnum.IDLE]: new IdleState(),
        [ModesEnum.CALIBRATION]: new CalibrationState(),
        [ModesEnum.REST]: new RestState(),
        [ModesEnum.STAND]: new StandState(),
        [ModesEnum.WALK]: new BezierState(),
        [ModesEnum.UNRECOGNIZED]: new IdleState()
    }
    let lastTick = performance.now()
    let lastRobotPosition = new Vector3()

    const dir = [1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1]
    const THREEJS_SCALE = 10

    let body_state = {
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: 0.15,
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
        'Smooth motion': true,
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: 0.15,
        zm: 0,
        Background: defaultColor
    }

    onMount(async () => {
        await populateModelCache()
        await createScene()
        servoAngles.subscribe(updateAnglesFromStore)
        walkGait.subscribe(gait => {
            const walkPlanner = planners[ModesEnum.WALK]
            if (!(walkPlanner instanceof BezierState)) {
                throw new Error(
                    `Expected BezierState for WALK mode, got ${walkPlanner.constructor.name}`
                )
            }
            walkPlanner.set_mode(gait.gait)
        })
        if (panel) createPanel()
    })

    onDestroy(() => {
        gui_panel?.destroy()
    })

    const updateAnglesFromStore = (angles: AnglesData) => {
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
        kinematicData.set(
            KinematicData.create({
                omega: settings.omega,
                phi: settings.phi,
                psi: settings.psi,
                xm: settings.xm,
                ym: settings.ym,
                zm: settings.zm
            })
        )
    }

    const setSceneBackground = (c: string | null) => (sceneManager.scene.background = new Color(c!))

    const updateAngles = (name: string, angle: number) => {
        modelTargetAngles.angles[$jointNames.indexOf(name)] = angle * (180 / Math.PI)
        servoAnglesOut.set(
            AnglesData.create({
                angles: modelTargetAngles.angles.map(num => Math.round(num))
            })
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
            sceneManager.addDragControl(angles => {
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
        modelTargetAngles.angles = new_angles
    }

    const orient_robot = (robot: URDFRobot, toes: Vector3[]) => {
        if (settings['Robot transform controls'] || !settings['Auto orient robot']) return
        robot.position.y = robot.position.y - Math.min(...toes.map(toe => toe.y))

        const cumulativeYaw = body_state.cumulative_yaw

        const cosYaw = Math.cos(cumulativeYaw)
        const sinYaw = Math.sin(cumulativeYaw)
        const rotatedXm = settings.xm * cosYaw - settings.zm * sinYaw
        const rotatedZm = settings.xm * sinYaw + settings.zm * cosYaw

        robot.position.x = smooth(
            robot.position.x,
            (-rotatedZm - body_state.cumulative_z) * THREEJS_SCALE,
            SMOOTH_AMOUNT
        )
        robot.position.z = smooth(
            robot.position.z,
            (-rotatedXm - body_state.cumulative_x) * THREEJS_SCALE,
            SMOOTH_AMOUNT
        )

        const pitch = degToRad(settings.psi - 90) + body_state.cumulative_pitch
        const roll = degToRad(settings.omega) + body_state.cumulative_roll

        robot.rotation.z = smooth(
            robot.rotation.z,
            degToRad(-settings.phi + $mpu.heading + 90) + cumulativeYaw,
            SMOOTH_AMOUNT
        )
        robot.rotation.y = smooth(robot.rotation.y, roll, SMOOTH_AMOUNT)
        robot.rotation.x = smooth(robot.rotation.x, pitch, SMOOTH_AMOUNT)
    }

    const update_camera = (robot: URDFRobot) => {
        const delta = robot.position.clone().sub(lastRobotPosition)
        sceneManager.orbit.target.add(delta)
        sceneManager.camera.position.add(delta)
        lastRobotPosition.copy(robot.position)
    }

    const smooth = (start: number, end: number, amount: number) => {
        return settings['Smooth motion'] ? lerp(start, end, amount) : end
    }

    const update_gait = () => {
        if (sceneManager.isDragging || !settings['Internal kinematic']) return
        const controlData = get(input)

        let planner = planners[get(mode).mode]
        const delta = performance.now() - lastTick
        lastTick = performance.now()

        body_state = planner.step(body_state, controlData, delta)

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
        settings.xm = robot.position.z / THREEJS_SCALE
        settings.zm = -robot.position.x / THREEJS_SCALE
    }

    const updateTargetPosition = () => {
        target.visible = settings['Target position']
        target.position.x = smooth(target.position.x, target_position.x, SMOOTH_AMOUNT)
        target.position.z = smooth(target.position.z, target_position.z, SMOOTH_AMOUNT)
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
            currentModelAngles.angles[i] = smooth(
                (robot.joints[$jointNames[i]].angle as number) * (180 / Math.PI),
                modelTargetAngles.angles[i],
                SMOOTH_AMOUNT
            )
            robot.joints[$jointNames[i]].setJointValue(degToRad(currentModelAngles.angles[i]))
        }

        orient_robot(robot, toes)
        updateTargetPosition()
    }
</script>

<svelte:window onresize={sceneManager.fillParent} />

<canvas bind:this={canvas}></canvas>
