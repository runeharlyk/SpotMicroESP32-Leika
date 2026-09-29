import loadMujoco, { type MainModule } from '@mujoco/mujoco'
import wasmUrl from '@mujoco/mujoco/mujoco.wasm?url'
import uzip from 'uzip'
import { resolve } from '$app/paths'
import type { SimAssets } from './pico-sim'

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

export async function loadSimulation(): Promise<{ mujoco: MainModule; assets: SimAssets }> {
    const [mujoco, sceneXml, zip, gaitCoef] = await Promise.all([
        mujocoModule(),
        fetchOk('spot_pico_scene.xml').then(response => response.text()),
        fetchOk('spot_pico.zip').then(response => response.arrayBuffer()),
        fetchOk('spot_pico_gait.json').then(response => response.json())
    ])
    const meshes = Object.fromEntries(
        Object.entries(uzip.parse(zip))
            .filter(([name]) => name.endsWith('.stl'))
            .map(([name, bytes]) => [name.slice(name.lastIndexOf('/') + 1), bytes])
    )
    return { mujoco, assets: { sceneXml, meshes, gaitCoef } }
}
