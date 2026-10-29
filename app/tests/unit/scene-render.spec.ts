import { describe, it, expect, beforeEach } from 'vitest'
import { BoxGeometry, Group, Mesh, MeshBasicMaterial, type WebGLRenderer } from 'three'
import type { URDFRobot } from 'urdf-loader'
import SceneBuilder from '../../src/lib/sceneBuilder'

// The scene builder with its real camera, orbit controls and scene; only the WebGL renderer, which jsdom lacks, is
// stood in for, so the frames the builder asks it to draw can be counted.
describe('the 3D scene', () => {
    let scene: SceneBuilder
    let frame: () => void
    let drawn: number
    let changes: boolean

    const draws = (frames = 1) => {
        const before = drawn
        for (let i = 0; i < frames; i++) frame()
        return drawn - before
    }

    beforeEach(() => {
        drawn = 0
        changes = false
        scene = new SceneBuilder()
        scene.renderer = {
            domElement: document.createElement('canvas'),
            setAnimationLoop: (loop: () => void) => (frame = loop),
            render: () => drawn++,
            setSize: () => {},
            setPixelRatio: () => {}
        } as unknown as WebGLRenderer
        scene
            .addPerspectiveCamera({ x: -0.5, y: 0.5, z: 1 })
            .addOrbitControls(2, 20, false)
            .addRenderCb(() => changes)
            .startRenderLoop()
    })

    it('draws the first frame, and no frame after it while nothing changes', () => {
        expect(draws()).toBe(1)
        expect(draws(10)).toBe(0)
    })

    it('draws each frame whose callback changed the scene, and only those', () => {
        draws()
        changes = true
        expect(draws(3)).toBe(3)
        changes = false
        expect(draws(3)).toBe(0)
    })

    it('draws while the orbit controls turn the camera', () => {
        draws()
        scene.orbit.autoRotate = true
        expect(draws(3)).toBe(3)
        scene.orbit.autoRotate = false
        expect(draws(3)).toBe(0)
    })

    it('draws once after a resize', () => {
        draws()
        scene.handleResize(640, 480)
        expect(draws(3)).toBe(1)
    })

    it('draws once when a mesh of the model arrives after the model, which then casts a shadow', () => {
        const model = new Group() as unknown as URDFRobot
        scene.addModel(model)
        draws()
        const mesh = new Mesh(new BoxGeometry(), new MeshBasicMaterial())
        model.add(mesh)
        expect(draws(3)).toBe(1)
        expect(mesh.castShadow).toBe(true)
    })
})
