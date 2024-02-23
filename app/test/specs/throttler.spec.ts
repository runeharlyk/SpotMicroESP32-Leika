import { throttler } from '../../src/lib/utilities';
import { describe, it, expect, beforeEach, afterEach, test, vitest } from 'vitest';

describe('throttler', () => {
	let throttleInstance: throttler;
	let callback;

	beforeEach(() => {
		vitest.useFakeTimers();
		throttleInstance = new throttler();
		callback = vitest.fn();
	});

	afterEach(() => {
		vitest.useRealTimers();
	});

	it('should call the callback function after the specified time', () => {
		throttleInstance.throttle(callback, 1000);
		expect(callback).not.toHaveBeenCalled();

		vitest.advanceTimersByTime(1000);
		expect(callback).toHaveBeenCalledTimes(1);
	});

	it('should not call the callback function if throttle is called again within the timeout period', () => {
		throttleInstance.throttle(callback, 1000);
		throttleInstance.throttle(callback, 1000);

		vitest.advanceTimersByTime(500);
		expect(callback).not.toHaveBeenCalled();

		vitest.advanceTimersByTime(500);
		expect(callback).toHaveBeenCalledTimes(1);
	});

	it('should allow the callback to be called again after the timeout period', () => {
		throttleInstance.throttle(callback, 1000);
		vitest.advanceTimersByTime(1000);
		expect(callback).toHaveBeenCalledTimes(1);

		throttleInstance.throttle(callback, 1000);
		vitest.advanceTimersByTime(1000);
		expect(callback).toHaveBeenCalledTimes(2);
	});
});
