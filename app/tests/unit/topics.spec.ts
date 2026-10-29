import { describe, expect, it } from 'vitest'
import type { MessageFns } from '../../src/lib/platform_shared/message'
import { MESSAGE_TYPE_TO_TAG } from '../../src/lib/stores/socket'
import { STATE_TOPICS } from '../../src/lib/transport/topics'
import { SHARED_TOPICS } from './topic-robot'

// The app's table of state topics against platform_shared/topics.json, which the firmware's table is tested against
// too: a topic the firmware answers with a snapshot that the app took for a stream, or the other way round, would
// leave a watch waiting for a value that never comes.
describe('the state topics', () => {
    it("are the shared table's state topics, in its order", () => {
        const shared = SHARED_TOPICS.topics.filter(t => t.kind === 'state').map(t => t.tag)
        const app = STATE_TOPICS.map(topic => MESSAGE_TYPE_TO_TAG.get(topic as MessageFns<unknown>))
        expect(app).toEqual(shared)
    })

    it('name members of the Message oneof by their tags', () => {
        const names = SHARED_TOPICS.topics.map(t => t.name)
        expect(new Set(names).size).toBe(names.length)
        for (const { tag } of SHARED_TOPICS.topics)
            expect([...MESSAGE_TYPE_TO_TAG.values()]).toContain(tag)
    })
})
