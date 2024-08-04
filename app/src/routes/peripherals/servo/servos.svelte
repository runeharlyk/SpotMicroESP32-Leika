<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import MotorOutline from '~icons/mdi/motor-outline';
	import Spinner from '$lib/components/Spinner.svelte';
	import { socket } from '$lib/stores';
	import { throttler as Throttler } from '$lib/utilities';
	import { onMount } from 'svelte';

    type ServoConfig = {
        servoIndex: number;
        conversion: number;
        centerPwm: number;
    };

    enum ServoCalibrationState { MinPwm=0, MinPwmTo180, MaxPwm, Save}

    enum CalibrationState { ServoCalibration, ServoFrameCalibration, Test }

    const StateTitle: Record<ServoCalibrationState, string> = {
        [ServoCalibrationState.MinPwm]: "Set min pwm value",
        [ServoCalibrationState.MinPwmTo180]: "Set min pwm to 180",
        [ServoCalibrationState.MaxPwm]: "Set max pwm value",
        [ServoCalibrationState.Save]: "Save config"
    };

    const StateDescription: Record<ServoCalibrationState, string> = {
        [ServoCalibrationState.MinPwm]: "Write description",
        [ServoCalibrationState.MinPwmTo180]: "Write description",
        [ServoCalibrationState.MaxPwm]: "Write description",
        [ServoCalibrationState.Save]: "Write description"
    };

    // Global servo config
    let servoEnabled = false;
    let servoUpdateIntervalMs = 2; 
    let minGlobalPWM = 100
    let pwm = 306;
    let maxGlobalPWM = 800
    let maxServoDegAngle = 87.5
    const numServos = 12

    // Individual servo calibration 
    let servoIndex = 0
    let minPwm = minGlobalPWM
    let maxPwm = maxGlobalPWM
    let minPwmTo180Deg = 500
    $: conversion = (minPwmTo180Deg - minPwm) / 180;
    $: centerPwm = (maxPwm - minPwm) / 2;
    let servoConfig:ServoConfig[] = []
    let state: ServoCalibrationState
    let calibrationState: CalibrationState

    // Servo frame calibration
    let servoDir = 1
    let centerAngleDeg = 0
    let servoOrder = []

    const next = () => {
        if (state == ServoCalibrationState.MinPwm) {
            minPwm = pwm + 0
            state = ServoCalibrationState.MinPwmTo180
        } else if (state == ServoCalibrationState.MinPwmTo180) {
            minPwmTo180Deg = pwm + 0
            state = ServoCalibrationState.MaxPwm
        } else if (state == ServoCalibrationState.MaxPwm) {
            maxPwm = pwm + 0
            servoConfig[servoIndex] = {
                servoIndex,
                conversion,
                centerPwm
            };
            servoIndex += 1
            state = servoIndex == 12 ? ServoCalibrationState.Save : ServoCalibrationState.MinPwm
            } else if (state === ServoCalibrationState.Save) {
                
                calibrationState = CalibrationState.ServoFrameCalibration
        }
    }

    onMount(() => {
        calibrationState = CalibrationState.ServoCalibration
        state = ServoCalibrationState.MinPwm
    })

    const sweepRange = () => console.log("Sweep range");

    const throttler = new Throttler()

    const sweep = (event:any) => {
        let channel = event.detail.channel;
        socket.sendEvent('servoConfiguration', {servos:[{channel, sweep: true}]});
    };

    const activateServo = (event:any) => {
        socket.sendEvent('servoState', {'active':1});
    };

    const deactivateServo = (event:any) => {
        socket.sendEvent('servoState', {'active':0});
    };

    const updatePWM = () => {
        throttler.throttle(() => {
            socket.sendEvent('servoPWM', {servo_id:servoIndex, pwm});
        }, 10)
    }
</script>

<SettingsCard collapsible={false}>
	<MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">Servo</span>
    <ul class="steps steps-vertical lg:steps-horizontal">
        <li class="step" class:step-primary={calibrationState == CalibrationState.ServoCalibration}>Servo calibration ({servoIndex}/{numServos})</li>
        <li class="step" class:step-primary={calibrationState == CalibrationState.ServoFrameCalibration}>Frame calibration</li>
        <li class="step" class:step-primary={calibrationState == CalibrationState.Test}>Test</li>
        <li class="step">Control</li>
    </ul>
    {#if calibrationState == CalibrationState.ServoCalibration}
        <ul class="steps steps-vertical lg:steps-horizontal">
            <li class="step" class:step-primary={state == ServoCalibrationState.MinPwm}>Set min pwm value</li>
            <li class="step" class:step-primary={state == ServoCalibrationState.MinPwmTo180}>Set pwm value for 180</li>
            <li class="step" class:step-primary={state == ServoCalibrationState.MaxPwm}>Set max pwm</li>
        </ul>

        <h2 class="text-lg">Servo {servoIndex}</h2>

        <input type="range" min={minGlobalPWM} max={maxGlobalPWM} bind:value={pwm} on:input={updatePWM} class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700">
        <div>PWM value: {pwm}</div>
        <div class="flex justify-end">
            <button class="btn btn-primary" on:click={next}>{StateTitle[state]}</button>
        </div>

        <div role="alert" class="alert">
            <svg
              xmlns="http://www.w3.org/2000/svg"
              fill="none"
              viewBox="0 0 24 24"
              class="stroke-info h-6 w-6 shrink-0">
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
            </svg>
            <span>{StateDescription[state]}</span>
        </div>

        <div class="mt-4">
            <h3 class="text-md font-semibold">Current Calibration Values:</h3>
            <ul>
                <li>Min PWM: {state === ServoCalibrationState.MinPwm ? pwm : minPwm}</li>
                <li>Min PWM to 180°: {state === ServoCalibrationState.MinPwmTo180 ? pwm : minPwmTo180Deg}</li>
                <li>Max PWM: {state === ServoCalibrationState.MaxPwm ? pwm : maxPwm}</li>
            </ul>
        </div>

        <div class="overflow-x-auto">
            <table class="table">
            <!-- head -->
            <thead>
                <tr>
                <th></th>
                <th>Min pwm</th>
                <th>Min pwn to 180 deg</th>
                <th>Max pwm</th>
                <th>Conversion</th>
                <th>Center pwm</th>
                </tr>
            </thead>
            <tbody>
                {#each servoConfig as config, index}
                    <tr class={index % 2 === 0 ? "bg-base-200" : ""}>
                        <th>{config.servoIndex}</th>
                        <td>{minPwm}</td>
                        <td>{minPwmTo180Deg}</td>
                        <td>{maxPwm}</td>
                        <td>{config.conversion.toFixed(2)}</td>
                        <td>{config.centerPwm.toFixed(2)}</td>
                    </tr>
                {/each}
            </tbody>
            </table>
        </div>
      {/if}
</SettingsCard>
