export class Result<T, E> {
    private constructor(
        private readonly isSuccess: boolean, 
        public readonly value?: T, 
        public readonly error?: E) {}
  
    public static ok<T>(value: T): Result<T, null> {
      return new Result<T, null>(true, value);
    }
  
    public static fail<E>(error: E): Result<null, E> {
      return new Result<null, E>(false, null, error);
    }
  
    public isOk(): this is Result<T, null> {
      return this.isSuccess;
    }
  
    public isErr(): this is Result<null, E> {
      return !this.isSuccess;
    }
}