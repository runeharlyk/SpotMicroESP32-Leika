<script lang="ts">
    import Lidar from "$lib/components/Lidar.svelte";
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import { lidar } from "$lib/stores/lidar";
	import { onMount } from "svelte";
	import { writable } from "svelte/store";
	import { distance } from "three/examples/jsm/nodes/Nodes.js";

    let port;
    let reader;
    let inputDone;
    let inputStream;
    let isConnected = false;
    let buffer = '';
    let lastLine = ""

    onMount(() => {
        navigator.serial.addEventListener("connect", (e) => {
            console.log("Connected");
        });

        navigator.serial.addEventListener("disconnect", (e) => {
            console.log("Disconnected");
        });

        navigator.serial.getPorts().then((ports) => {
        // Initialize the list of available ports with `ports` on page load.
        });
    })

    const connect = async () => {
        try {
            port = await navigator.serial.requestPort();
            await port.open({ baudRate: 115200 });
            const decoder = new TextDecoderStream();
            inputDone = port.readable.pipeTo(decoder.writable);
            inputStream = decoder.readable.pipeThrough(new TransformStream(new LineBreakTransformer()));
            reader = inputStream.getReader();
            readLoop();
        } catch (err) {
            console.error('Failed to open serial port:', err);
        }
    }

    async function readLoop() {
        while (true) {
            const { value, done } = await reader.read();
            if (done) {
                console.log('[readLoop] DONE', done);
                reader.releaseLock();
                break;
            }
            if (value.split(",").length !== 3) continue
                
            const [distance, angle, quality] = value.split(",").map((val:string) => parseFloat(val))
            const lidarData = { distance, angle, quality }
            
            if (distance <1000 || distance > 40000 || quality < 40) continue
            lidar.addData(lidarData)
        }
    }

    class LineBreakTransformer {
		container: string;
        constructor() {
            this.container = '';
        }

        transform(chunk: any, controller: { enqueue: (arg0: any) => any; }) {
            let re = /\r\n|\n|\r/gm;
            this.container += chunk;
            const lines = this.container.split(re);
            this.container = lines.pop() || "";
            lines.forEach(line => controller.enqueue(line));
        }

        flush(controller: { enqueue: (arg0: string) => void; }) {
        controller.enqueue(this.container);
        }
    }
</script>

<SettingsCard collapsible={false}>
    <!-- <MdiConnection slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" /> -->
    <span slot="title">Lidar</span>
    <div>
        <button on:click={connect} class="btn">Connect</button>
    </div>
</SettingsCard>

<div class="h-96 w-96">
    <div class="w-full h-full">
        <Lidar />
    </div>
</div>