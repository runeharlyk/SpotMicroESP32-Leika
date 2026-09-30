#!/usr/bin/env node
import { readFileSync, readdirSync, writeFileSync } from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import uzip from 'uzip'
import { picoUrdfForApp, PICO_MESH_PACKAGE } from './pico-model.js'

const appRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const resources = path.resolve(appRoot, '..', 'simulation', 'src', 'resources', 'spot_pico')
const staticDir = path.join(appRoot, 'static')

const urdf = readFileSync(path.join(resources, 'spot_pico.urdf'), 'utf8')
const scene = readFileSync(path.join(resources, 'scene.xml'), 'utf8')
writeFileSync(path.join(staticDir, 'spot_pico.urdf'), picoUrdfForApp(urdf, scene))

// The browser simulation loads the simulation's own scene and gait tuning (lib/simulation).
writeFileSync(path.join(staticDir, 'spot_pico_scene.xml'), scene)
writeFileSync(
    path.join(staticDir, 'spot_pico_gait.json'),
    readFileSync(path.join(resources, 'gait_coef.json'))
)

const meshDir = path.join(resources, 'meshes')
const meshes = Object.fromEntries(
    readdirSync(meshDir)
        .filter(name => name.endsWith('.stl'))
        .map(name => [`${PICO_MESH_PACKAGE}/${name}`, readFileSync(path.join(meshDir, name))])
)
writeFileSync(path.join(staticDir, 'spot_pico.zip'), Buffer.from(uzip.encode(meshes)))

console.log(`Pico model written to static/ (${Object.keys(meshes).length} meshes)`)

// Spot Micro and Yertle: their generated scenes (simulation/generate_scenes.py), and Yertle's STL meshes.
const generated = path.resolve(resources, '..')
writeFileSync(
    path.join(staticDir, 'sim_spot_micro.xml'),
    readFileSync(path.join(generated, 'spot_micro', 'scene.xml'))
)
writeFileSync(
    path.join(staticDir, 'sim_yertle.xml'),
    readFileSync(path.join(generated, 'yertle', 'scene.xml'))
)
const yertleMeshDir = path.join(staticDir, 'URDF')
const yertleMeshes = Object.fromEntries(
    readdirSync(yertleMeshDir)
        .filter(name => name.endsWith('.stl'))
        .map(name => [name, readFileSync(path.join(yertleMeshDir, name))])
)
writeFileSync(path.join(staticDir, 'sim_yertle_meshes.zip'), Buffer.from(uzip.encode(yertleMeshes)))
// The 3D view's copy of the same meshes, under the package path yertle.URDF names them by.
writeFileSync(
    path.join(staticDir, 'URDF.zip'),
    Buffer.from(
        uzip.encode(
            Object.fromEntries(
                Object.entries(yertleMeshes).map(([name, mesh]) => [`URDF/${name}`, mesh])
            )
        )
    )
)

console.log('Spot Micro and Yertle scenes written to static/')
