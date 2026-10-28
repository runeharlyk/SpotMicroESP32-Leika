import { describe, it, expect } from 'vitest'
import { selectableModes } from '../../src/lib/stores/model-store'
import { ModesEnum } from '../../src/lib/platform_shared/message'

describe('mode selector', () => {
    it('offers the learned walk only on a robot that reports a policy', () => {
        expect(selectableModes(false)).not.toContain(ModesEnum.WALK_NN)
        expect(selectableModes(true)).toContain(ModesEnum.WALK_NN)
    })

    it('keeps every classic mode either way', () => {
        const classic = [ModesEnum.DEACTIVATED, ModesEnum.REST, ModesEnum.STAND, ModesEnum.WALK]
        for (const walkNn of [false, true])
            expect(selectableModes(walkNn)).toEqual(expect.arrayContaining(classic))
    })
})
