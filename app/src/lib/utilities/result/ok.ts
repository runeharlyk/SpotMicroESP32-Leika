export class Ok<T> {
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
  isOk(): this is Ok<T> {
    return true
  }

  /**
   * Type guard for `Err`
   * @returns `true` if `Err`; `false` if `Ok`
   */
  isErr(): false {
    return false
  }

  /**
   * Create an `Ok`
   * @param inner
   * @returns `Ok(inner)`
   */
  static new<T>(inner: T): Ok<T> {
    return new Ok<T>(inner)
  }

  /**
   * Create an empty `Ok`
   * @returns `Ok(void)` 
   */
  static void(): Ok<void> {
    return new Ok(undefined)
  }
}
