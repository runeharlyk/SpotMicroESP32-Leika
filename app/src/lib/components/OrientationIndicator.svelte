<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import * as THREE from 'three'
  import { imu } from '$lib/stores/imu'
  import SceneBuilder from '$lib/sceneBuilder'

  let canvas: HTMLCanvasElement = $state()
  let sceneBuilder: SceneBuilder
  let cube: THREE.Mesh
  let targetRotation = new THREE.Euler()
  let lastUpdateTime = 0
  const LERP_SPEED = 5 // rotations per second

  const initThreeJS = () => {
    sceneBuilder = new SceneBuilder()
      .addRenderer({ canvas: canvas, antialias: true, alpha: true })
      .addPerspectiveCamera({ x: 2, y: 0, z: 2 })
      .addOrbitControls(1, 10, false)
      .addAmbientLight({ color: 0x404040, intensity: 0.5 })
      .addDirectionalLight({ color: 0xffffff, intensity: 3, x: 10, y: 20, z: 7 })
      .fillParent()

    const geometry = new THREE.BoxGeometry(1, 1, 1)
    const material = new THREE.MeshPhongMaterial({
      color: 0x00ff00,
      transparent: true,
      opacity: 0.8
    })
    cube = new THREE.Mesh(geometry, material)
    sceneBuilder.scene.add(cube)

    sceneBuilder.addRenderCb(() => {
      if (!cube) return
      const currentTime = performance.now()
      const deltaTime = (currentTime - lastUpdateTime) / 1000 // convert to seconds
      lastUpdateTime = currentTime

      const lerpFactor = Math.min(1, LERP_SPEED * deltaTime)
      cube.rotation.x = THREE.MathUtils.lerp(cube.rotation.x, targetRotation.x, lerpFactor)
      cube.rotation.y = THREE.MathUtils.lerp(cube.rotation.y, targetRotation.y, lerpFactor)
      cube.rotation.z = THREE.MathUtils.lerp(cube.rotation.z, targetRotation.z, lerpFactor)
    })

    sceneBuilder.startRenderLoop()
  }

  const updateOrientation = () => {
    if (!cube) return

    const y = -$imu.x[$imu.x.length - 1] || 0
    const x = $imu.y[$imu.y.length - 1] || 0
    const z = -$imu.z[$imu.z.length - 1] || 0

    targetRotation.set(
      THREE.MathUtils.degToRad(x),
      THREE.MathUtils.degToRad(y),
      THREE.MathUtils.degToRad(z)
    )
  }

  onMount(() => {
    initThreeJS()
  })

  onDestroy(() => {
    sceneBuilder?.renderer?.dispose()
  })

  $effect(() => {
    if ($imu) {
      updateOrientation()
    }
  })
</script>

<div class="h-60 w-60 border-2 border-base-300 rounded-md">
  <canvas class="w-full h-full" bind:this={canvas}></canvas>
</div>
