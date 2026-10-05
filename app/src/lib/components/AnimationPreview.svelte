<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import {
        Group,
        Mesh,
        MeshBasicMaterial,
        Raycaster,
        SphereGeometry,
        Vector2,
        Vector3,
        type Object3D
    } from 'three'
    import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js'
    import type { URDFRobot } from 'urdf-loader'
    import SceneBuilder from '$lib/sceneBuilder'
    import { model } from '$lib/stores/model-store'
    import { populateModelCache } from '$lib/utilities/model-utilities'
    import { kinConfig, type Variant } from '$lib/simulation/firmware/kin-config'
    import { BodyState } from '$lib/simulation/firmware/kinematics'
    import { DIR } from '$lib/simulation/firmware/motion'
    import { jointTargets } from '$lib/simulation/controllers'
    import { JOINT_MAPS } from '$lib/simulation/robots'
    import { poseToAngles, stancePose, toBodyState, type Vec3 } from '$lib/animation/player'
    import type { Frame } from '$lib/animation/editor'
    import {
        bodyTransform,
        fitModelFrame,
        footHandleOffset,
        footHandleScene,
        type ModelFrame
    } from '$lib/animation/preview'

    interface Props {
        variant: Variant
        frame: Frame
        /** Foot legs of the selected keyframe, with their offsets (mm), that can be dragged. */
        handles?: { leg: number; offset: Vec3 }[]
        onMove?: (leg: number, offset: Vec3) => void
        /** Called every rendered frame with the seconds since the last, to advance playback. */
        onTick?: (dt: number) => void
    }

    const { variant, frame, handles = [], onMove, onTick }: Props = $props()

    const HANDLE_RADIUS = 0.12
    const HANDLE_COLOR = 0xffcc00
    const HANDLE_ACTIVE_COLOR = 0xff6600

    let canvas: HTMLCanvasElement
    let problem = $state<string | null>(null)
    const scene = new SceneBuilder()
    const body = new Group()
    const handleGroup = new Group()
    const spheres: Mesh[] = []
    let robot: URDFRobot | undefined
    let modelFrame = $state<ModelFrame | undefined>()
    // Lowers the model so the firmware's feet, at world height 0, stand on the ground plane.
    let lift = 0
    let control: TransformControls | undefined
    let dragging = false
    let last = performance.now()
    const cfg = $derived(kinConfig(variant))

    const pose = (angles: number[]) => {
        const values = jointTargets(
            JOINT_MAPS[variant],
            angles.map((a, i) => a * DIR[i])
        )
        for (const [name, value] of Object.entries(values))
            robot?.joints[name]?.setJointValue(value)
    }

    /** Each leg's toe under its knee joint, so the fit pairs every toe with its own leg. */
    const toes = (): Vector3[] | null => {
        const map = JOINT_MAPS[variant]
        const found = [0, 1, 2, 3].map(leg => {
            let toe: Object3D | undefined
            robot?.joints[map.joints[leg * 3 + 2]]?.traverse(child => {
                if (!toe && child.name.includes('toe')) toe = child
            })
            return toe?.getWorldPosition(new Vector3())
        })
        return found.every(Boolean) ? (found as Vector3[]) : null
    }

    const fit = () => {
        const stance = poseToAngles(cfg, stancePose(), cfg.defaultBodyHeight)
        pose(stance.angles)
        robot!.updateMatrixWorld(true)
        const standing = new BodyState(cfg)
        const feet = standing.feet.map(([x, y, z]) => [x, y - standing.ym, z])
        const found = toes()
        if (!found) {
            problem = 'This model has no toes to place it by, so its body does not move.'
            return
        }
        modelFrame = fitModelFrame(
            feet,
            found.map(v => v.toArray()),
            robot!.scale.x
        )
        lift = -modelFrame.origin[1]
        handleGroup.position.y = lift
    }

    const show = (f: Frame) => {
        if (!robot) return
        pose(f.angles)
        if (!modelFrame) return
        const { rotation: r, translation: t } = bodyTransform(
            modelFrame,
            toBodyState(cfg, f.pose.body, f.base)
        )
        body.matrix.set(
            r[0][0],
            r[0][1],
            r[0][2],
            t[0],
            r[1][0],
            r[1][1],
            r[1][2],
            t[1] + lift,
            r[2][0],
            r[2][1],
            r[2][2],
            t[2],
            0,
            0,
            0,
            1
        )
    }

    const placeHandles = (list: { leg: number; offset: Vec3 }[]) => {
        if (!modelFrame || dragging) return
        spheres.forEach((sphere, leg) => {
            const handle = list.find(h => h.leg === leg)
            sphere.visible = !!handle
            if (handle)
                sphere.position.fromArray(footHandleScene(modelFrame!, cfg, leg, handle.offset))
        })
        if (control?.object && !control.object.visible) control.detach()
    }

    const grab = (event: PointerEvent) => {
        if (!control || dragging) return
        const rect = canvas.getBoundingClientRect()
        const pointer = new Vector2(
            ((event.clientX - rect.left) / rect.width) * 2 - 1,
            -((event.clientY - rect.top) / rect.height) * 2 + 1
        )
        const ray = new Raycaster()
        ray.setFromCamera(pointer, scene.camera)
        const hit = ray.intersectObjects(spheres.filter(s => s.visible))[0]
        if (!hit) return
        spheres.forEach(s =>
            (s.material as MeshBasicMaterial).color.set(
                s === hit.object ? HANDLE_ACTIVE_COLOR : HANDLE_COLOR
            )
        )
        control.attach(hit.object)
    }

    const render = () => {
        const now = performance.now()
        onTick?.(Math.min(0.1, (now - last) / 1000))
        last = now
    }

    onMount(async () => {
        await populateModelCache()
        const loaded = get(model) as URDFRobot | undefined
        if (!loaded) {
            problem = 'The 3D model did not load.'
            return
        }
        // The shared model, not a clone: its meshes load after it does, so a clone would have none. Visualization
        // adds it back to its own scene when it mounts.
        robot = loaded
        body.matrixAutoUpdate = false
        body.add(robot)
        scene
            .addRenderer({ antialias: true, canvas, alpha: true })
            .addPerspectiveCamera({ x: -2.5, y: 2, z: 3 })
            .addOrbitControls(1, 12, false)
            .addDirectionalLight({ x: 10, y: 20, z: 10, color: 0xffffff, intensity: 3 })
            .addAmbientLight({ color: 0xffffff, intensity: 0.5 })
            .addGroundPlane({ y: 0 })
            .fillParent()
            .addRenderCb(render)
            .startRenderLoop()
        scene.scene.add(body, handleGroup)
        fit()
        for (let leg = 0; leg < 4; leg++) {
            const sphere = new Mesh(
                new SphereGeometry(HANDLE_RADIUS, 16, 12),
                new MeshBasicMaterial({ color: HANDLE_COLOR })
            )
            sphere.visible = false
            sphere.userData.leg = leg
            spheres.push(sphere)
            handleGroup.add(sphere)
        }
        control = new TransformControls(scene.camera, scene.renderer.domElement)
        control.setMode('translate')
        control.setSize(0.6)
        control.addEventListener('dragging-changed', event => {
            dragging = event.value as boolean
            scene.orbit.enabled = !dragging
            const moved = control?.object
            if (!dragging && moved && modelFrame)
                onMove?.(
                    moved.userData.leg,
                    footHandleOffset(modelFrame, cfg, moved.userData.leg, moved.position.toArray())
                )
        })
        scene.scene.add(control.getHelper())
        canvas.addEventListener('pointerdown', grab)
        const resize = new ResizeObserver(() => scene.fillParent())
        resize.observe(canvas.parentElement!)
        disconnect = () => resize.disconnect()
        show(frame)
        placeHandles(handles)
    })

    let disconnect = () => {}
    onDestroy(() => {
        disconnect()
        canvas?.removeEventListener('pointerdown', grab)
        control?.dispose()
        scene.renderer?.setAnimationLoop(null)
        scene.renderer?.dispose()
    })

    $effect(() => show(frame))
    $effect(() => placeHandles(handles))
</script>

<div class="relative h-full min-h-72 w-full">
    <canvas bind:this={canvas}></canvas>
    {#if problem}
        <p class="absolute top-2 left-2 text-sm opacity-75">{problem}</p>
    {/if}
    {#if modelFrame && modelFrame.residual > 0.3}
        <p class="absolute bottom-2 left-2 text-sm text-warning">
            The model's feet sit {(modelFrame.residual * 100).toFixed(0)} mm from the firmware's; the
            preview is approximate.
        </p>
    {/if}
</div>
