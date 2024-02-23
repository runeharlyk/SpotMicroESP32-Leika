import { describe, it, expect } from 'vitest';
import { Result } from '../src/lib/utilities';

describe('Result', () => {
	it('should create a success result correctly', () => {
		const successValue = 'Success value';
		const result = Result.ok(successValue);

		expect(result.isOk()).toBe(true);
		expect(result.isErr()).toBe(false);
		expect(result.inner).toBe(successValue);
	});

	it('should create an error result correctly', () => {
		const errorMessage = 'Error message';
		const result = Result.err(errorMessage);

		expect(result.isOk()).toBe(false);
		expect(result.isErr()).toBe(true);
		expect(result.inner).toBe(errorMessage);
	});

	it('should type guard success and error results correctly', () => {
		const successResult = Result.ok(123);
		const errorResult = Result.err('Error');

		if (successResult.isOk()) {
			expect(typeof successResult.inner).toBe('number');
		} else {
			throw new Error('Expected successResult to be ok');
		}

		if (errorResult.isErr()) {
			expect(typeof errorResult.inner).toBe('string');
		} else {
			throw new Error('Expected errorResult to be fail');
		}
	});
});
