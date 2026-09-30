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
