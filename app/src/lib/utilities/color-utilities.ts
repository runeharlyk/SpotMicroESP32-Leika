export const daisyColor = (name: string, opacity: number = 100) => {
    const color = getComputedStyle(document.documentElement).getPropertyValue(name).trim()
    if (opacity >= 100) return color
    const alpha = Math.min(Math.max(opacity, 0), 100) / 100
    return `${color.replace(/(\/\s*\d+(\.\d+)?\))|\)$/, '')} / ${alpha})`
}
