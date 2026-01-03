<script lang="ts">
    import { onMount } from 'svelte'
    import { socket } from '$lib/stores'

    //import { IMUReport, IMUType } from '$lib/platform_shared/example';
    import { AnglesData, WebsocketMessage, IMUData, CorrelationRequest, CorrelationResponse } from '$lib/platform_shared/websocket_message'

    // const imu_report: IMUReport = {type: IMUType.IMU_ACCEL, xVal: 4}
    // const writer = IMUReport.encode(imu_report);
    // const bytes = writer.finish();
    // // Convert bytes to hex
    // const hex = Array.from(bytes)
    // 	.map((b) => b.toString(16).padStart(2, '0'))
    // 	.join(' ');

    // const wmd: WebsocketMessage = { imu: {temp: 0, x: 0, y: 0, z: 1}, angles: {angles: [2]}}
    // const wmd: WebsocketMessage = { imu: {temp: 0, x: 0, y: 0, z: 0} }
    // const wmd: WebsocketMessage = { rssi: { rssi: 16 } }
    // const wmd: WebsocketMessage = { imu: {temp: 1, x: 2, y: 4, z: 5} }
    // const wmd: WebsocketMessage = { angles: {angles: [1,2,3,4]} }
    const wmd: WebsocketMessage = { pongmsg: {} }
    const writer = WebsocketMessage.encode(wmd)
    const bytes = writer.finish()
    // Convert bytes to hex
    const hex = Array.from(bytes)
        .map(b => b.toString(16).padStart(2, '0'))
        .join(' ')

    // const decodedmsg: WebsocketMessage = WebsocketMessage.decode(bytes);
    // const objects = Object.entries(decodedmsg)

    // console.log(Object.keys(AnglesData.create())[0] )

    // console.log(objects)

    // console.log(Object.keys(decodedmsg))
    // if (Object.values(decodedmsg)[6] instanceof AnglesData) {
    //     console.log("True0!")
    // }
    // if (Object.values(decodedmsg)[0] instanceof AnglesData) {
    //     console.log("True1!")
    // }
    // if (Object.values(decodedmsg)[1] instanceof AnglesData) {
    //     console.log("True2!")
    // }

    const handleData = (data: IMUData) => {
        // console.log(data)
    }
    onMount(() => {
        socket.on(CorrelationResponse, (data) => {
            console.log(data)
        })
        socket.onEvent('open', () => {
            socket.sendEvent(CorrelationRequest, CorrelationRequest.create({correlationId: 69, featuresDataRequest: { sonarTest: true }}))
        })

        return socket.on(IMUData, handleData)
    })
</script>

<h1>Hexadecimal Output</h1>

<p><strong>Hex output:</strong> {hex}</p>
