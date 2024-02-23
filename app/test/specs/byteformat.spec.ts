import { describe, it, expect } from 'vitest';
import { humanFileSize } from '../../src/lib/utilities';

describe('humanFileSize', () => {
  it('returns "0B" for 0 bytes', () => {
    expect(humanFileSize(0)).toBe('0B');
  });

  it('returns the size in bytes correctly', () => {
    expect(humanFileSize(500)).toBe('500B');
  });

  it('returns the size in kB correctly', () => {
    expect(humanFileSize(1024)).toBe('1kB');
  });

  it('returns the size in MB correctly', () => {
    expect(humanFileSize(1048576)).toBe('1MB'); // 1024 * 1024
  });

  it('returns the size in GB correctly', () => {
    expect(humanFileSize(1073741824)).toBe('1GB'); // 1024 * 1024 * 1024
  });

  it('rounds to 2 decimal places correctly', () => {
    expect(humanFileSize(1536)).toBe('1.5kB'); // 1024 + 512
  });
});