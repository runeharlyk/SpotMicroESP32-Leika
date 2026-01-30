<script lang="ts">
    interface Props {
        heading?: number
        size?: string
    }

    let { heading = 0, size = 'w-48 h-48' }: Props = $props()

    const getCardinalDirection = (h: number) => {
        if (h >= 337.5 || h < 22.5) return 'N'
        if (h >= 22.5 && h < 67.5) return 'NE'
        if (h >= 67.5 && h < 112.5) return 'E'
        if (h >= 112.5 && h < 157.5) return 'SE'
        if (h >= 157.5 && h < 202.5) return 'S'
        if (h >= 202.5 && h < 247.5) return 'SW'
        if (h >= 247.5 && h < 292.5) return 'W'
        return 'NW'
    }

    const ticks = [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]
</script>

<div class="flex flex-col items-center">
    <div class="relative {size}">
        <svg viewBox="0 0 200 200" class="w-full h-full">
            <circle
                cx="100"
                cy="100"
                r="90"
                fill="none"
                stroke="currentColor"
                stroke-width="2"
                class="opacity-30"
            />
            <circle
                cx="100"
                cy="100"
                r="70"
                fill="none"
                stroke="currentColor"
                stroke-width="1"
                class="opacity-20"
            />
            <circle
                cx="100"
                cy="100"
                r="50"
                fill="none"
                stroke="currentColor"
                stroke-width="1"
                class="opacity-20"
            />

            <text x="100" y="20" text-anchor="middle" class="fill-current text-sm font-bold">N</text>
            <text x="180" y="105" text-anchor="middle" class="fill-current text-sm font-bold">E</text>
            <text x="100" y="190" text-anchor="middle" class="fill-current text-sm font-bold">S</text>
            <text x="20" y="105" text-anchor="middle" class="fill-current text-sm font-bold">W</text>

            {#each ticks as tick}
                <line
                    x1={100 + 85 * Math.sin((tick * Math.PI) / 180)}
                    y1={100 - 85 * Math.cos((tick * Math.PI) / 180)}
                    x2={100 + 78 * Math.sin((tick * Math.PI) / 180)}
                    y2={100 - 78 * Math.cos((tick * Math.PI) / 180)}
                    stroke="currentColor"
                    stroke-width={tick % 90 === 0 ? 2 : 1}
                    class="opacity-50"
                />
            {/each}

            <g transform="rotate({heading}, 100, 100)">
                <polygon points="100,25 93,100 100,90 107,100" class="fill-error" />
                <polygon points="100,175 93,100 100,110 107,100" class="fill-base-300" />
            </g>

            <circle cx="100" cy="100" r="8" class="fill-base-content" />
        </svg>
    </div>
    <div class="text-2xl font-mono font-bold mt-2">{heading.toFixed(1)}°</div>
    <div class="text-sm opacity-70">{getCardinalDirection(heading)}</div>
</div>
