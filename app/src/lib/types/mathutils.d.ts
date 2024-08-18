declare module 'three/src/math/MathUtils' {
	export function generateUUID(): string;
	export function clamp(value: number, min: number, max: number): number;
	export function euclideanModulo(n: number, m: number): number;
	export function mapLinear(x: number, a1: number, a2: number, b1: number, b2: number): number;
	export function lerp(x: number, y: number, t: number): number;
	export function smoothstep(x: number, min: number, max: number): number;
	export function smootherstep(x: number, min: number, max: number): number;
	export function randInt(low: number, high: number): number;
	export function randFloat(low: number, high: number): number;
	export function randFloatSpread(range: number): number;
	export function degToRad(degrees: number): number;
	export function radToDeg(radians: number): number;
	export function isPowerOfTwo(value: number): boolean;
	export function ceilPowerOfTwo(value: number): number;
	export function floorPowerOfTwo(value: number): number;
}
