import { describe, it, expect } from 'vitest';
import { toUint8, toInt8 } from '../../src/lib/utilities';

describe('toUint8', () => {
	it('min interval value should get 0', () => {
		expect(toUint8(-1, -1, 1)).toBe(0);
	});
	it('middle interval value should get 128', () => {
		expect(toUint8(0, -1, 1)).toBe(128);
	});

	it('max interval value should get 255', () => {
		expect(toUint8(1, -1, 1)).toBe(255);
	});

	it('min value should be clamped', () => {
		expect(toUint8(-2, -1, 1)).toBe(0);
	});

	it('max value should be clamped', () => {
		expect(toUint8(2, -1, 1)).toBe(255);
	});
});

describe('toInt8', () => {
	it('min interval value should get -128', () => {
		expect(toInt8(-1, -1, 1)).toBe(-128);
	});
	it('middle interval value should get 0', () => {
		expect(toInt8(0, -1, 1)).toBe(0);
	});

	it('max interval value should get 127', () => {
		expect(toInt8(1, -1, 1)).toBe(127);
	});

	it('min value should be clamped', () => {
		expect(toInt8(-2, -1, 1)).toBe(-128);
	});

	it('max value should be clamped', () => {
		expect(toInt8(2, -1, 1)).toBe(127);
	});
});


