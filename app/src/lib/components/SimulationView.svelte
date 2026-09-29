<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import { Group, Vector3 } from 'three'
    import type { URDFRobot } from 'urdf-loader'
    import SceneBuilder from '$lib/sceneBuilder'
    import { input, mode, reportedVariant, variants, walkGait } from '$lib/stores'
    import { persistentStore } from '$lib/utilities'
    import { cacheModelFiles, loadModel } from '$lib/utilities/model-utilities'
    import { loadSimulation } from '$lib/simulation/load'
    import { RobotSim } from '$lib/simulation/robot-sim'
    import { resolveChoice, ROBOTS, type SimChoice } from '$lib/simulation/robots'
    import LoadError from './LoadError.svelte'

    // The 3D view draws models ten times their size in metres; the simulation matches it.
    const MODEL_SCALE = 10

    let canvas: HTMLCanvasElement
    let status = $state<'loading' | 'running' | 'error'>('loading')
    let error = $state<unknown>()
    let fallen = $state(false)
    let engineReady = $state(false)
    const savedChoice = persistentStore<Partial<SimChoice> | null>('simulation_choice', null)
    let chosen = $state(resolveChoice(get(savedChoice), get(reportedVariant)))

    const sceneManager = new SceneBuilder()
    let resize: ResizeObserver | undefined
    let sim: RobotSim | undefined
    let robot: URDFRobot | undefined
    let lastFrame = 0
    let destroyed = false
    let latestLoad = 0
    const followed = new Vector3()
    const robotWorld = new Vector3()

    async function loadRobotModel(variant: (typeof variants)[keyof typeof variants]) {
        await cacheModelFiles(variant.stl)
        const result = await loadModel(variant.model, variant.modelYaw)
        if (result.isErr()) throw new Error(result.inner)
        return result.inner[0]
    }

    // A load that another choice, or leaving the view, overtook builds nothing.
    const overtaken = (load: number) => destroyed || load !== latestLoad

    async function start() {
        const load = ++latestLoad
        const { robot: definition, controller } = chosen
        const variant = variants[definition.variant]
        sim?.dispose()
        sim = undefined
        robot?.parent?.removeFromParent()
        robot = undefined
        fallen = false
        status = 'loading'
        try {
            const [{ mujoco, scene, gaitCoef }, model] = await Promise.all([
                loadSimulation(definition),
                loadRobotModel(variant)
            ])
            if (overtaken(load)) return
            sim = new RobotSim(
                mujoco,
                scene,
                controller.create({ gaitCoef }),
                definition.footRadius
            )
            placeRobot(model, variant.modelYaw)
            lastFrame = performance.now()
            engineReady = true
            status = 'running'
        } catch (cause) {
            if (overtaken(load)) return
            error = cause
            status = 'error'
        }
    }

    function choose(choice: Partial<SimChoice>) {
        chosen = resolveChoice(choice, undefined)
        savedChoice.set({ robot: chosen.robot.id, controller: chosen.controller.id })
        void start()
    }

    /**
     * The robot sits in a group that maps MuJoCo's Z-up world, in metres, onto the scene the same
     * way the 3D view turns and scales its models, so MuJoCo's base pose can be copied onto it as is.
     */
    function placeRobot(model: URDFRobot, modelYaw: number) {
        const world = new Group()
        world.rotation.set(-Math.PI / 2, 0, Math.PI / 2 + modelYaw)
        world.scale.setScalar(MODEL_SCALE)
        model.rotation.set(0, 0, 0)
        model.scale.setScalar(1)
        world.add(model)
        sceneManager.scene.add(world)
        robot = model
        followed.set(0, 0, 0)
    }

    function frame() {
        if (!sim || !robot) return
        const now = performance.now()
        sim.setControls({
            input: get(input),
            mode: get(mode).mode,
            gait: get(walkGait).gait,
            imu: [0, 0]
        })
        sim.step((now - lastFrame) / 1000)
        lastFrame = now

        const [x, y, z] = sim.basePosition()
        const [w, qx, qy, qz] = sim.baseQuaternion()
        robot.position.set(x, y, z)
        robot.quaternion.set(qx, qy, qz, w)
        for (const [name, angle] of Object.entries(sim.jointAngles())) {
            robot.joints[name]?.setJointValue(angle)
        }
        followRobot()

        const hasFallen = sim.hasFallen()
        if (hasFallen !== fallen) fallen = hasFallen
    }

    // The camera travels with the robot, so a walking robot stays in view at the chosen angle.
    function followRobot() {
        robot!.getWorldPosition(robotWorld)
        robotWorld.y = 0
        const delta = robotWorld.clone().sub(followed)
        sceneManager.camera.position.add(delta)
        sceneManager.orbit.target.add(delta)
        followed.copy(robotWorld)
    }

    const reset = () => {
        sim?.reset()
        lastFrame = performance.now()
    }

    onMount(() => {
        sceneManager
            .addRenderer({ antialias: true, canvas, alpha: true })
            .addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
            .addOrbitControls(0.5, 20, false)
            .addDirectionalLight({ x: 10, y: 20, z: 10, color: 0xffffff, intensity: 3 })
            .addAmbientLight({ color: 0xffffff, intensity: 0.5 })
            .addFogExp2(0xcccccc, 0.015)
            .addGroundPlane()
            .fillParent()
            .addRenderCb(frame)
            .startRenderLoop()

        const parent = canvas.parentElement
        if (parent) {
            resize = new ResizeObserver(() => sceneManager.fillParent())
            resize.observe(parent)
        }
        void start()
    })

    onDestroy(() => {
        destroyed = true
        sceneManager.stopRenderLoop()
        sceneManager.renderer?.dispose()
        resize?.disconnect()
        sim?.dispose()
    })
</script>

<div class="relative h-full w-full">
    <canvas bind:this={canvas}></canvas>

    <div class="bg-base-100/70 rounded-box absolute top-2 left-2 flex flex-col gap-1 p-2 text-xs">
        <select
            name="robot"
            aria-label="Simulated robot"
            class="select select-xs"
            value={chosen.robot.id}
            onchange={event => choose({ robot: event.currentTarget.value as SimChoice['robot'] })}
        >
            {#each ROBOTS as candidate (candidate.id)}
                <option value={candidate.id}>{candidate.label}</option>
            {/each}
        </select>
        <select
            name="controller"
            aria-label="Controller"
            class="select select-xs"
            value={chosen.controller.id}
            disabled={chosen.robot.controllers.length < 2}
            onchange={event =>
                choose({
                    robot: chosen.robot.id,
                    controller: event.currentTarget.value as SimChoice['controller']
                })}
        >
            {#each chosen.robot.controllers as candidate (candidate.id)}
                <option value={candidate.id}>{candidate.label}</option>
            {/each}
        </select>
        <span class="opacity-70">Physics approximate</span>
    </div>

    {#if status === 'loading'}
        <div class="absolute inset-0 flex flex-col items-center justify-center gap-2">
            <span class="loading loading-spinner loading-lg text-primary"></span>
            <span class="text-sm">
                {engineReady ?
                    `Loading ${chosen.robot.label}`
                :   'Loading the physics engine (10 MB)'}
            </span>
        </div>
    {:else if status === 'error'}
        <div class="absolute inset-x-4 top-12">
            <LoadError {error} retry={start} />
        </div>
    {:else if fallen}
        <div
            role="status"
            class="bg-base-100/80 rounded-box absolute top-12 left-1/2 flex -translate-x-1/2 items-center gap-3 px-3 py-2 text-sm"
        >
            The {chosen.robot.label} fell over
            <button class="btn btn-sm btn-primary" onclick={reset}>Reset</button>
        </div>
    {/if}
</div>
