<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import { Group, Vector3 } from 'three'
    import type { URDFRobot } from 'urdf-loader'
    import SceneBuilder from '$lib/sceneBuilder'
    import { currentVariant, input, mode, variants } from '$lib/stores'
    import { cacheModelFiles, loadModel } from '$lib/utilities/model-utilities'
    import { loadSimulation } from '$lib/simulation/load'
    import { PicoSim } from '$lib/simulation/pico-sim'
    import { simulationCommand } from '$lib/simulation/controls'
    import LoadError from './LoadError.svelte'

    const pico = variants.SPOTMICRO_ESP32_MINI
    // The 3D view draws models ten times their size in metres; the simulation matches it.
    const MODEL_SCALE = 10

    let canvas: HTMLCanvasElement
    let status = $state<'loading' | 'running' | 'error'>('loading')
    let error = $state<unknown>()
    let fallen = $state(false)

    const sceneManager = new SceneBuilder()
    let resize: ResizeObserver | undefined
    let sim: PicoSim | undefined
    let robot: URDFRobot | undefined
    let lastFrame = 0
    const followed = new Vector3()
    const robotWorld = new Vector3()

    async function loadPicoModel() {
        await cacheModelFiles(pico.stl)
        const result = await loadModel(pico.model, pico.modelYaw)
        if (result.isErr()) throw new Error(result.inner)
        return result.inner[0]
    }

    async function start() {
        status = 'loading'
        try {
            const [{ mujoco, assets }, model] = await Promise.all([
                loadSimulation(),
                loadPicoModel()
            ])
            sim?.dispose()
            sim = new PicoSim(mujoco, assets)
            placeRobot(model)
            lastFrame = performance.now()
            status = 'running'
        } catch (cause) {
            error = cause
            status = 'error'
        }
    }

    /**
     * The robot sits in a group that maps MuJoCo's Z-up world, in metres, onto the scene the same
     * way the 3D view turns and scales its models, so MuJoCo's base pose can be copied onto it as is.
     */
    function placeRobot(model: URDFRobot) {
        if (robot) robot.parent?.removeFromParent()
        const world = new Group()
        world.rotation.set(-Math.PI / 2, 0, Math.PI / 2 + pico.modelYaw)
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
        sim.setCommand(simulationCommand(get(input), get(mode).mode))
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

    // The camera travels with the robot, so a walking Pico stays in view at the chosen angle.
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
        sceneManager.stopRenderLoop()
        sceneManager.renderer?.dispose()
        resize?.disconnect()
        sim?.dispose()
    })
</script>

<div class="relative h-full w-full">
    <canvas bind:this={canvas}></canvas>

    <div
        class="bg-base-100/70 rounded-box pointer-events-none absolute top-2 left-2 px-2 py-1 text-xs"
    >
        Simulated Pico
        {#if $currentVariant !== pico}
            <span class="opacity-70">- the simulation always runs the Pico</span>
        {/if}
    </div>

    {#if status === 'loading'}
        <div class="absolute inset-0 flex flex-col items-center justify-center gap-2">
            <span class="loading loading-spinner loading-lg text-primary"></span>
            <span class="text-sm">Loading the physics engine (10 MB)</span>
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
            The Pico fell over
            <button class="btn btn-sm btn-primary" onclick={reset}>Reset</button>
        </div>
    {/if}
</div>
