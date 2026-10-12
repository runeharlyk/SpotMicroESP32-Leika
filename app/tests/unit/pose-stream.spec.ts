import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { POSE_INTERVAL_MS, createPoseStream } from '../../src/lib/animation/pose-stream'
import { stancePose, type Pose } from '../../src/lib/animation/player'

const lifted = (z: number): Pose => {
    const pose = stancePose()
    pose.body[5] = z
    return pose
}

// A robot that answers each pose when the test says so, as a slow link would.
function slowRobot() {
    const sent: number[] = []
    const answers: Array<(error?: Error) => void> = []
    const send = (pose: Pose) =>
        new Promise<void>((resolve, reject) => {
            sent.push(pose.body[5])
            answers.push(error => (error ? reject(error) : resolve()))
        })
    const answer = async (error?: Error) => {
        answers.shift()!(error)
        await vi.advanceTimersByTimeAsync(0)
    }
    return { sent, send, answer }
}

describe('pose stream', () => {
    beforeEach(() => vi.useFakeTimers())
    afterEach(() => vi.useRealTimers())

    it('sends the first pose at once', () => {
        const robot = slowRobot()
        createPoseStream(robot.send, () => {}).show(lifted(1))
        expect(robot.sent).toEqual([1])
    })

    it('sends only the newest of the poses that came while one was on its way', async () => {
        const robot = slowRobot()
        const stream = createPoseStream(robot.send, () => {})
        stream.show(lifted(1))
        for (const z of [2, 3, 4]) stream.show(lifted(z))
        await vi.advanceTimersByTimeAsync(POSE_INTERVAL_MS)
        expect(robot.sent).toEqual([1])
        await robot.answer()
        expect(robot.sent).toEqual([1, 4])
    })

    it('keeps POSE_INTERVAL_MS between two poses however fast the robot answers', async () => {
        const robot = slowRobot()
        const stream = createPoseStream(robot.send, () => {})
        stream.show(lifted(1))
        await robot.answer()
        stream.show(lifted(2))
        await vi.advanceTimersByTimeAsync(POSE_INTERVAL_MS - 1)
        expect(robot.sent).toEqual([1])
        await vi.advanceTimersByTimeAsync(1)
        expect(robot.sent).toEqual([1, 2])
    })

    it('ends at the first refusal, reports it once and sends nothing after it', async () => {
        const robot = slowRobot()
        const refused = vi.fn()
        const stream = createPoseStream(robot.send, refused)
        stream.show(lifted(1))
        stream.show(lifted(2))
        await robot.answer(new Error('Stand first'))
        await vi.advanceTimersByTimeAsync(POSE_INTERVAL_MS * 2)
        stream.show(lifted(3))
        expect(robot.sent).toEqual([1])
        expect(refused).toHaveBeenCalledOnce()
        expect(refused.mock.calls[0][0].message).toBe('Stand first')
    })

    it('sends nothing once closed, and reports no refusal that comes after', async () => {
        const robot = slowRobot()
        const refused = vi.fn()
        const stream = createPoseStream(robot.send, refused)
        stream.show(lifted(1))
        stream.show(lifted(2))
        stream.close()
        await robot.answer(new Error('gone'))
        await vi.advanceTimersByTimeAsync(POSE_INTERVAL_MS * 2)
        expect(robot.sent).toEqual([1])
        expect(refused).not.toHaveBeenCalled()
    })
})
