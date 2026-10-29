<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import { Color, Mesh, MeshPhongMaterial, type Object3D } from 'three'
    import { lerp } from 'three/src/math/MathUtils'
    import type { URDFRobot } from 'urdf-loader'
    import { model, modelVariant, socket } from '$lib/stores'
    import { populateModelCache, getToeWorldPositions } from '$lib/utilities/model-utilities'
    import { followRobotPose, type JointValues } from '$lib/robot-pose'
    import SceneBuilder from '$lib/sceneBuilder'

    interface Props {
        defaultColor?: string | null
        orbit?: boolean
        ground?: boolean
        /** Joints to show instead of the ones the robot reports; given, the view asks the robot for nothing. */
        joints?: JointValues
        /** A joint whose link, the part it turns, is drawn in the highlight colour. */
        highlight?: string
    }

    let {
        defaultColor = '#0091ff',
        orbit = false,
        ground = true,
        joints,
        highlight
    }: Props = $props()

    const sceneManager = new SceneBuilder()
    let canvas: HTMLCanvasElement
    // The robot reports at 10 Hz; easing towards each report keeps the picture fluid between them.
    const SMOOTH_AMOUNT = 0.2

    let reportedJoints: JointValues = {}
    let stopFollowing: (() => void) | undefined
    let destroyed = false

    onMount(() => {
        // Read with the model's loading, so the joints followed are those of the model shown.
        if (!joints)
            stopFollowing = followRobotPose(
                socket,
                get(modelVariant),
                reported => (reportedJoints = reported)
            )
        void showModel()
    })

    onDestroy(() => {
        destroyed = true
        stopFollowing?.()
        if (sceneManager.renderer) sceneManager.stopRenderLoop()
        disconnectObservers.forEach(disconnect => disconnect())
        // The model is shared with the next view that shows it.
        highlighted.forEach(unhighlight)
        HIGHLIGHT.dispose()
    })

    const showModel = async () => {
        await populateModelCache()
        if (destroyed) return
        createScene()
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
        if (defaultColor) sceneManager.scene.background = new Color(defaultColor)
    }

    // A toe this close to the ground stands on it: the world matrices' rounding would otherwise move the robot, and
    // draw it again, every frame.
    const GROUND_TOLERANCE = 1e-6

    // Keeps the lowest toe on the ground, whatever the legs do; whether that moved the robot.
    const standOnGround = (robot: URDFRobot) => {
        const toes = getToeWorldPositions(robot)
        const lowest = toes.length ? Math.min(...toes.map(toe => toe.y)) : 0
        if (Math.abs(lowest) < GROUND_TOLERANCE) return false
        robot.position.y -= lowest
        return true
    }

    const HIGHLIGHT = new MeshPhongMaterial({ color: 0xff9f1c })
    // Each highlighted mesh keeps its own material in its userData, to get it back.
    let highlighted: Mesh[] = []
    const unhighlight = (mesh: Mesh) => (mesh.material = mesh.userData.unhighlighted)

    // The link's own meshes, not those of the links below its child joints. Meshes load after the model, so this
    // runs every frame and catches them as they arrive.
    const linkMeshes = (part: Object3D, meshes: Mesh[] = []): Mesh[] => {
        for (const child of part.children) {
            if ((child as { isURDFJoint?: boolean }).isURDFJoint) continue
            if (child instanceof Mesh) meshes.push(child)
            linkMeshes(child, meshes)
        }
        return meshes
    }

    // Whether a mesh changed its material.
    const showHighlight = (robot: URDFRobot) => {
        const meshes =
            highlight && robot.joints[highlight] ? linkMeshes(robot.joints[highlight]) : []
        const left = highlighted.filter(mesh => !meshes.includes(mesh))
        left.forEach(unhighlight)
        let changed = left.length > 0
        for (const mesh of meshes)
            if (mesh.material !== HIGHLIGHT) {
                mesh.userData.unhighlighted = mesh.material
                mesh.material = HIGHLIGHT
                changed = true
            }
        highlighted = meshes
        return changed
    }

    // Whether the frame moved a joint, the robot or a highlight: the joints ease towards their targets and then rest.
    const render = () => {
        const robot = sceneManager.model
        if (!robot) return false
        let changed = false
        for (const [name, target] of Object.entries(joints ?? reportedJoints)) {
            const joint = robot.joints[name]
            if (joint)
                changed =
                    joint.setJointValue(lerp(joint.angle as number, target, SMOOTH_AMOUNT)) ||
                    changed
        }
        changed = standOnGround(robot) || changed
        return showHighlight(robot) || changed
    }
</script>

<canvas bind:this={canvas}></canvas>
