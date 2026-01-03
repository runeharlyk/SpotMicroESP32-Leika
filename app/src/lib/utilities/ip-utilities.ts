export function ipToUint32(ip: string): number {
    const parts = ip.split('.')
    if (parts.length !== 4) return 0
    return (
        (parseInt(parts[0], 10) |
            (parseInt(parts[1], 10) << 8) |
            (parseInt(parts[2], 10) << 16) |
            (parseInt(parts[3], 10) << 24)) >>>
        0
    )
}

export function uint32ToIp(ip: number): string {
    return [ip & 0xff, (ip >>> 8) & 0xff, (ip >>> 16) & 0xff, (ip >>> 24) & 0xff].join('.')
}

export function isValidIpString(ip: string | undefined): boolean {
    if (!ip) return false
    const regexExp =
        /\b(?:(?:2(?:[0-4][0-9]|5[0-5])|[0-1]?[0-9]?[0-9])\.){3}(?:(?:2([0-4][0-9]|5[0-5])|[0-1]?[0-9]?[0-9]))\b/
    return regexExp.test(ip)
}

