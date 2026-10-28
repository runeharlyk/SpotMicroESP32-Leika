import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import wasmUrl from '@mujoco/mujoco/mujoco.wasm?url'
import uzip from 'uzip'
import { resolve } from '$app/paths'
import type { SimScene } from './robot-sim'
import type { RobotDefinition } from './robots'

let loading: Promise<MainModule> | undefined

/** The WASM is fetched once per page and shared by every Simulation view; a failed fetch is retried. */
const mujocoModule = () =>
    (loading ??= loadMujoco({ locateFile: () => wasmUrl }).catch(error => {
        loading = undefined
        throw error
    }))

const fetchOk = async (path: string) => {
    const response = await fetch(`${resolve('/')}${path}`)
    if (!response.ok) throw new Error(`Could not load ${path}: HTTP ${response.status}`)
    return response
}

const meshesFrom = async (zipFile: string | null) => {
    if (!zipFile) return {}
    const files = uzip.parse(await fetchOk(zipFile).then(response => response.arrayBuffer()))
    return Object.fromEntries(
        Object.entries(files)
            .filter(([name]) => name.endsWith('.stl'))
            .map(([name, bytes]) => [name.slice(name.lastIndexOf('/') + 1), bytes])
    )
}

/** The engine, and a robot's scene and meshes. */
export async function loadSimulation(
    robot: RobotDefinition
): Promise<{ mujoco: MainModule; scene: SimScene }> {
    const [mujoco, sceneXml, meshes] = await Promise.all([
        mujocoModule(),
        fetchOk(robot.sceneFile).then(response => response.text()),
        meshesFrom(robot.meshZip)
    ])
    return { mujoco, scene: { id: robot.id, sceneXml, meshes } }
}
