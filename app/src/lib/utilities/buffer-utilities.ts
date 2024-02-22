export class throttler {
    private _throttlePause: boolean;
    constructor() {
        this._throttlePause = false;
    }
    throttle = (callback:Function, time:number) => {
        if (this._throttlePause) return;
      
        this._throttlePause = true;
        setTimeout(() => {
          callback();
          this._throttlePause = false;
        }, time);
    };
}

