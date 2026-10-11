<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import { Color } from 'three'
    import { lerp } from 'three/src/math/MathUtils'
    import { GUI } from 'three/addons/libs/lil-gui.module.min.js'
    import type { URDFRobot } from 'urdf-loader'
    import { model, modelVariant, socket } from '$lib/stores'
    import { populateModelCache, getToeWorldPositions } from '$lib/utilities/model-utilities'
    import { followRobotPose, type JointValues } from '$lib/robot-pose'
    import SceneBuilder from '$lib/sceneBuilder'

    interface Props {
        defaultColor?: string | null
        orbit?: boolean
        panel?: boolean
        ground?: boolean
    }

    let { defaultColor = '#0091ff', orbit = false, panel = true, ground = true }: Props = $props()

    const sceneManager = new SceneBuilder()
    let canvas: HTMLCanvasElement
    let gui_panel: GUI | undefined
    // The robot reports at 10 Hz; easing towards each report keeps the picture fluid between them.
    const SMOOTH_AMOUNT = 0.2

    let targetJoints: JointValues = {}
    let stopFollowing: (() => void) | undefined
    let destroyed = false

    const settings = {
        'Smooth motion': true,
        Background: defaultColor
    }

    onMount(() => {
        // Read with the model's loading, so the joints followed are those of the model shown.
        stopFollowing = followRobotPose(
            socket,
            get(modelVariant),
            joints => (targetJoints = joints)
        )
        void showModel()
    })

    onDestroy(() => {
        destroyed = true
        stopFollowing?.()
        if (sceneManager.renderer) sceneManager.stopRenderLoop()
        gui_panel?.destroy()
        disconnectObservers.forEach(disconnect => disconnect())
    })

    const showModel = async () => {
        await populateModelCache()
        if (destroyed) return
        createScene()
        if (panel) createPanel()
        observeCanvas()
    }

    const disconnectObservers: (() => void)[] = []

    // Track the canvas itself rather than the window: the parent can resize without the window
    // doing so, and rendering a scene that is scrolled out of view is pure battery drain on the
    // phone driving the robot.
    const observeCanvas = () => {
        const parent = canvas.parentElement
        if (!parent) return

        const resize = new ResizeObserver(() => sceneManager.fillParent())
        resize.observe(parent)

        const visibility = new IntersectionObserver(entries => {
            if (entries.some(entry => entry.isIntersecting)) sceneManager.startRenderLoop()
            else sceneManager.stopRenderLoop()
        })
        visibility.observe(canvas)

        disconnectObservers.push(
            () => resize.disconnect(),
            () => visibility.disconnect()
        )
    }

    const createPanel = () => {
        gui_panel = new GUI({ width: 310 })
        gui_panel.close()
        gui_panel.domElement.id = 'three-gui-panel'

        const visibility = gui_panel.addFolder('Visualization')
        visibility.add(settings, 'Smooth motion')
        visibility.addColor(settings, 'Background').onChange(setSceneBackground).listen()
    }

    const setSceneBackground = (c: string | null) => (sceneManager.scene.background = new Color(c!))

    const createScene = () => {
        sceneManager
            .addRenderer({ antialias: true, canvas, alpha: true })
            .addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
            .addOrbitControls(2, 20, orbit)
            .addDirectionalLight({ x: 10, y: 20, z: 10, color: 0xffffff, intensity: 3 })
            .addAmbientLight({ color: 0xffffff, intensity: 0.5 })
            .addFogExp2(0xcccccc, 0.015)
            .addModel(get(model) as URDFRobot)
            .fillParent()
            .addRenderCb(render)
            .startRenderLoop()

        if (ground) sceneManager.addGroundPlane()
        if (defaultColor) setSceneBackground(settings['Background'] || defaultColor)
    }

    const smooth = (start: number, end: number) =>
        settings['Smooth motion'] ? lerp(start, end, SMOOTH_AMOUNT) : end

    // Keeps the lowest toe on the ground, whatever the legs do.
    const standOnGround = (robot: URDFRobot) => {
        const toes = getToeWorldPositions(robot)
        if (toes.length) robot.position.y -= Math.min(...toes.map(toe => toe.y))
    }

    const render = () => {
        const robot = sceneManager.model
        if (!robot) return
        for (const [name, target] of Object.entries(targetJoints)) {
            const joint = robot.joints[name]
            if (joint) joint.setJointValue(smooth(joint.angle as number, target))
        }
        standOnGround(robot)
    }
</script>

<canvas bind:this={canvas}></canvas>
