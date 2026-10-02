// Adapts the simulation's spot_pico URDF (simulation/src/resources/spot_pico) for the app's 3D view,
// so the robot has one model: meshes move to the package the app caches from the mesh zip, and
// each tibia gets the toe frame the view uses to stand the robot on the ground.

const LEGS = ['fr', 'fl', 'rr', 'rl']

/**
 * Toe offsets in each tibia frame, from the foot sites of the MuJoCo scene.
 * @param {string} sceneXml
 */
const footSites = sceneXml =>
    Object.fromEntries(
        LEGS.map(leg => {
            const site = sceneXml.match(new RegExp(`<site name="foot_${leg}" pos="([^"]+)"`))
            if (!site) throw new Error(`scene.xml has no foot site for ${leg}`)
            return [leg, site[1]]
        })
    )

/**
 * @param {string} urdf the simulation's spot_pico.urdf
 * @param {string} sceneXml the simulation's scene.xml, which holds the foot sites
 */
export function picoUrdfForApp(urdf, sceneXml) {
    const feet = footSites(sceneXml)
    const toes = LEGS.map(
        leg => `  <link name="${leg}_toe_link"/>
  <joint name="${leg}_toe" type="fixed">
    <parent link="${leg}_tibia"/>
    <child link="${leg}_toe_link"/>
    <origin xyz="${feet[leg]}" rpy="0 0 0"/>
  </joint>
`
    ).join('')

    return urdf
        .replace(/filename="\.\.\/meshes\/(\w+\.stl)"/g, 'filename="package://spot_pico/$1"')
        .replace('</robot>', `${toes}</robot>`)
}

export const PICO_MESH_PACKAGE = 'spot_pico'
