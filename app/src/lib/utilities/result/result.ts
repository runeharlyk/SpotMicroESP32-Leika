import { Err } from './err'
import { Ok } from './ok'

export type Result<T = unknown, E = unknown> = Ok<T> | Err<E>

export namespace Result {
  /**
   * @returns `Ok<T>`
   */
  export function ok<T = unknown>(value: T) {
    return Ok.new(value)
  }

  /**
   * @returns `Err<E>`
   */
  export function err<E = unknown>(error: E) {
    return Err.new(error)
  }
}
