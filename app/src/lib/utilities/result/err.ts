export class Err<T, U> {
  #inner: T
  #exception?: U

  constructor(inner: T, exception?: U) {
    this.#inner = inner
    this.#exception = exception
  }

  get inner(): T {
    return this.#inner
  }

  get exception(): U | undefined {
    return this.#exception;
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
  isErr(): this is Err<T, U> {
    return true
  }

  /**
   * Create an `Err`
   * @param inner
   * @returns `Err(inner)`
   */
  static new<E, F>(inner: E, exception: F): Err<E, F> {
    return new Err<E, F>(inner, exception)
  }
}
