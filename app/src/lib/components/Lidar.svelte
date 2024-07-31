<script lang="ts">
	import { onMount } from "svelte";

    function getIntersection(angle:number, size:number):number {
            const sinAngle = Math.sin(angle);
            const cosAngle = Math.cos(angle);

            let x, y;
            if (Math.abs(cosAngle) > Math.abs(sinAngle)) {
                x = size * Math.sign(cosAngle);
                y = x * sinAngle / cosAngle;
            } else {
                y = size * Math.sign(sinAngle);
                x = y * cosAngle / sinAngle;
            }

            return Math.sqrt(x**2 + y**2);
        }

    export let points:[number, number][] = new Array(360)
    .fill(5000) // 5m in mm as that allow us to use uint16 (2 bytes)
    .map((distance, angle) => [angle, getIntersection(angle, distance)])

    let canvas:HTMLCanvasElement
    let ctx


    onMount(() => {
        ctx = canvas.getContext("2d")
        resize()      
    })

    const draw = () => {
        const centerX = canvas.width / 2
        const centerY = canvas.height / 2
        
        const scale = 0.05

        const offsetDistance = 200;
        const offset = offsetDistance * scale;

        if (!ctx) return
        for (let i = 0; i < points.length; i++){
            const [angle, distance] = points[i]
            
            const startX = centerX + offset * Math.cos(angle);
            const startY = centerY + offset * Math.sin(angle);
            const endX = centerX + (offset + distance * scale) * Math.cos(angle);
            const endY = centerY + (offset + distance * scale) * Math.sin(angle);

            ctx.beginPath();
            ctx.moveTo(startX, startY);
            ctx.lineTo(endX, endY);
            ctx.strokeStyle = "grey"
            ctx.stroke();

            ctx.beginPath();
            ctx.arc(endX, endY, 3, 0, Math.PI * 2);
            ctx.fillStyle = "#1bfc06"
            ctx.fill();
        }
    }

    const resize = () => {
        const parentElement = canvas.parentElement;
		if (parentElement) {
			canvas.width = parentElement.clientWidth
            canvas.height = parentElement.clientHeight
		}
        draw()
    }

</script>

<svelte:window on:resize={resize}></svelte:window>

<canvas bind:this={canvas} class="w-full h-full"></canvas>