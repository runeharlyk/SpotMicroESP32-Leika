import { writable, derived } from 'svelte/store'
import { socket } from './socket'
import { MessageTopic, type SkillStatus, type SkillCommand } from '$lib/types/models'

const defaultStatus: SkillStatus = {
    x: 0,
    y: 0,
    z: 0,
    yaw: 0,
    distance: 0,
    skill_active: false,
    skill_target_x: 0,
    skill_target_z: 0,
    skill_target_yaw: 0,
    skill_traveled_x: 0,
    skill_traveled_z: 0,
    skill_rotated: 0,
    skill_progress: 0,
    skill_complete: false
}

function createSkillStore() {
    const status = writable<SkillStatus>(defaultStatus)
    const history = writable<SkillCommand[]>([])
    let unsubscribe: (() => void) | null = null

    function init() {
        if (unsubscribe) return
        unsubscribe = socket.on<SkillStatus>(MessageTopic.skillStatus, data => {
            status.set(data)
            if (data.event === 'complete') {
                history.update(h => [...h.slice(-9), getCurrentTarget(data)])
            }
        })
    }

    function getCurrentTarget(s: SkillStatus): SkillCommand {
        return { x: s.skill_target_x, z: s.skill_target_z, yaw: s.skill_target_yaw }
    }

    function execute(cmd: SkillCommand) {
        socket.sendEvent(MessageTopic.skill, cmd)
    }

    function walk(x: number, z: number = 0, yaw: number = 0, speed: number = 0.5) {
        execute({ x, z, yaw, speed })
    }

    function turn(yaw: number, speed: number = 0.5) {
        execute({ x: 0, z: 0, yaw, speed })
    }

    function stop() {
        socket.sendEvent(MessageTopic.displacement, { action: 'clear' })
    }

    function resetPosition() {
        socket.sendEvent(MessageTopic.displacement, { action: 'reset' })
    }

    function destroy() {
        if (unsubscribe) {
            unsubscribe()
            unsubscribe = null
        }
    }

    return {
        status,
        history,
        init,
        destroy,
        execute,
        walk,
        turn,
        stop,
        resetPosition,
        isActive: derived(status, $s => $s.skill_active),
        progress: derived(status, $s => $s.skill_progress),
        position: derived(status, $s => ({ x: $s.x, y: $s.y, z: $s.z, yaw: $s.yaw }))
    }
}

export const skill = createSkillStore()

