<script lang="ts">
	import { onMount } from "svelte";
    import { lidar, type LidarPoint } from '$lib/stores/lidar'

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


    let canvas:HTMLCanvasElement
    let ctx

    const DEG2RAD = 0.017453292519943;

    onMount(() => {
        ctx = canvas.getContext("2d")
        resize()      
        lidar.subscribe(lidar => {
            draw(lidar.points)
        })
    })

    const draw = (points:LidarPoint[]) => {
        if(!points) return
        const centerX = canvas.width / 2
        const centerY = canvas.height / 2
        
        const scale = 0.01//Math.max(centerX, centerY) / Math.max(...points.map((point) => point.distance))

        if (!ctx) return
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        for (let i = 0; i < points.length; i++){
            const angle = points[i].angle
            const distance = points[i].distance
            const quality = points[i].quality

            const endX = centerX + (distance * scale) * Math.cos(angle * DEG2RAD);
            const endY = centerY - (distance * scale) * Math.sin(angle * DEG2RAD);

            ctx.beginPath();
            ctx.moveTo(centerX, centerY);
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
    }

</script>

<svelte:window on:resize={resize}></svelte:window>

<canvas bind:this={canvas} class="w-full h-full"></canvas>