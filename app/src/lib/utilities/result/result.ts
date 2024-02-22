import { Err } from './err'
import { Ok } from './ok'

export type Result<T = unknown, E = unknown, F = unknown> = Ok<T> | Err<E, F>

export namespace Result {
  /**
   * @returns `Ok<T>`
   */
  export function ok<T = unknown>(value: T) {
    return Ok.new(value)
  }

  /**
   * @returns `Err<E, F>`
   */
  export function err<E = unknown, F = unknown>(error: E, exception?: F) {
    return Err.new(error, exception)
  }
}
