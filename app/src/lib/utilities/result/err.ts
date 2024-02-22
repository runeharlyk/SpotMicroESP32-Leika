export class Err<T> {
  #inner: T

  constructor(inner: T) {
    this.#inner = inner
  }

  get inner(): T {
    return this.#inner
  }

  /**
   * Type guard for `Ok`
   * @returns `true` if `Ok`; `false` if `Err`
   */
  isOk(): false {
    return false
  }

  /**
   * Type guard for `Err`
   * @returns `true` if `Err`; `false` if `Ok`
   */
  isErr(): this is Err<T> {
    return true
  }

  /**
   * Create an `Err`
   * @param inner
   * @returns `Err(inner)`
   */
  static new<E>(inner: E): Err<E> {
    return new Err<E>(inner)
  }
}
