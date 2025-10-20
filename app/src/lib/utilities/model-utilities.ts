import { Color, LoaderUtils, Vector3 } from 'three'
import URDFLoader, { type URDFRobot } from 'urdf-loader'
import { XacroLoader } from 'xacro-parser'
import { Result } from '$lib/utilities'
import { currentVariant, jointNames, model } from '$lib/stores'
import uzip from 'uzip'
import { fileService } from '$lib/services'
import { get } from 'svelte/store'
import { resolve } from '$app/paths'

let model_xml: XMLDocument

export const populateModelCache = async () => {
    await cacheModelFiles()
    const modelRes = await loadModel(get(currentVariant).model)
    if (modelRes.isOk()) {
        const [urdf, JOINT_NAME] = modelRes.inner
        jointNames.set(JOINT_NAME)
        model.set(urdf)
    } else {
        console.error(modelRes.inner, { exception: modelRes.exception })
    }
}

export const cacheModelFiles = async () => {
    const data = await fetch(get(currentVariant).stl)

    const files = uzip.parse(await data.arrayBuffer())

    for (const [path, data] of Object.entries(files) as [path: string, data: Uint8Array][]) {
        const normalizedPath = path.startsWith('/') ? path : '/' + path
        const resolvedUrl = resolve(normalizedPath as any)
        fileService?.saveFile(resolvedUrl, data)
        fileService?.saveFile(normalizedPath, data)
    }
}

export const loadModel = async (url: string): Promise<Result<[URDFRobot, string[]], string>> => {
    const urdfLoader = new URDFLoader()

    let xml =
        url.endsWith('.xacro') ? await loadXacro(url) : await fetch(url).then(res => res.text())

    if (typeof xml === 'string') {
        xml = new window.DOMParser().parseFromString(xml, 'text/xml')
    }

    return new Promise(resolve => {
        model_xml = xml
        try {
            const model = urdfLoader.parse(xml)
            setupRobot(model)
            const joints = Object.entries(model.joints)
                .filter(joint => joint[1].jointType !== 'fixed')
                .map(joint => joint[0])

            resolve(Result.ok([model, joints]))
        } catch (error) {
            resolve(Result.err('Failed to load model', error))
        }
    })
}

const loadXacro = async (url: string): Promise<XMLDocument> =>
    new Promise((resolve, reject) => {
        new XacroLoader().load(url, resolve, reject)
    })

function setupRobot(robot: URDFRobot) {
    robot.rotation.x = -Math.PI / 2
    robot.rotation.z = Math.PI / 2
    robot.scale.setScalar(10)
    robot.traverse(c => (c.castShadow = true))
    robot.updateMatrixWorld(true)
}

export function getToeWorldPositions(robot: URDFRobot): Vector3[] {
    const toes: Vector3[] = []
    robot.traverse(c => {
        if (c.name.includes('toe') && !c.name.includes('_link'))
            toes.push(c.getWorldPosition(new Vector3()))
    })
    return toes
}

export const extractFootColor = () => {
    const colorElem = model_xml.querySelector('material[name=foot_color] > color') as Element
    const colorAttrStr = colorElem.getAttribute('rgba') as string
    const colorStr = colorAttrStr
        .split(' ')
        .slice(0, 3)
        .map(val => Math.floor(+val * 255))
        .join(', ')

    return new Color(`rgb(${colorStr})`)
}
