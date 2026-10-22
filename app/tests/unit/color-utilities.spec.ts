import { afterEach, describe, expect, it } from 'vitest'
import { daisyColor } from '../../src/lib/utilities/color-utilities'

const theme = (name: string, value: string) =>
    document.documentElement.style.setProperty(name, value)

describe('daisyColor', () => {
    afterEach(() => document.documentElement.removeAttribute('style'))

    it('returns a theme colour as the theme gives it at full opacity', () => {
        theme('--color-primary', '#00bfff')
        expect(daisyColor('--color-primary')).toBe('#00bfff')
    })

    it('fades a hex theme colour, as app.css gives the primary and secondary', () => {
        theme('--color-primary', '#00bfff')
        expect(daisyColor('--color-primary', 50)).toBe('#00bfff80')
        theme('--color-primary', '#0bf')
        expect(daisyColor('--color-primary', 10)).toBe('#00bbff1a')
    })

    it("fades daisyUI's own oklch colours, replacing an alpha they already carry", () => {
        theme('--color-base-content', 'oklch(27.807% 0.029 256.847)')
        expect(daisyColor('--color-base-content', 10)).toBe('oklch(27.807% 0.029 256.847 / 0.1)')
        theme('--color-base-content', 'oklch(27.807% 0.029 256.847 / 0.5)')
        expect(daisyColor('--color-base-content', 10)).toBe('oklch(27.807% 0.029 256.847 / 0.1)')
    })
})
