/**
 * A theme colour (a CSS custom property) at `opacity` percent, as a colour string Chart.js can draw with. The theme
 * gives daisyUI's colours as oklch() and app.css the primary and secondary as hex, so both forms take the alpha.
 */
export const daisyColor = (name: string, opacity: number = 100) => {
    const color = getComputedStyle(document.documentElement).getPropertyValue(name).trim()
    if (opacity >= 100) return color
    const alpha = Math.min(Math.max(opacity, 0), 100) / 100
    if (color.startsWith('#')) return fullHex(color) + hexByte(alpha * 255)
    return `${color.replace(/\s*\/\s*[\d.]+%?\s*\)$|\)$/, '')} / ${alpha})`
}

const hexByte = (value: number) => Math.round(value).toString(16).padStart(2, '0')

// #rgb and #rrggbb as #rrggbb, dropping an alpha the colour carries.
const fullHex = (color: string) => {
    const digits = color.slice(1)
    const rgb =
        digits.length <= 4 ? [...digits.slice(0, 3)].map(d => d + d).join('') : digits.slice(0, 6)
    return `#${rgb}`
}
