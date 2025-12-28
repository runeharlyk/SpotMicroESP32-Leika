<script lang="ts">
	import { IMUReport, IMUType } from '$lib/platform_shared/example';

	const imu_report: IMUReport = {type: IMUType.IMU_ACCEL, xVal: 4}
	const writer = IMUReport.encode(imu_report);
    const bytes = writer.finish();
	// Convert bytes to hex
	const hex = Array.from(bytes)
		.map((b) => b.toString(16).padStart(2, '0'))
		.join(' ');
</script>

<h1>Hexadecimal Output</h1>

<p><strong>Hex output:</strong> {hex}</p>
