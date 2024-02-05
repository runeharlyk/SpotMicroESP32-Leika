import { writable } from 'svelte/store';

export const humanFileSize = (size:number):string => {
    var i = size == 0 ? 0 : Math.floor(Math.log(size) / Math.log(1024));
    return Number((size / Math.pow(1024, i)).toFixed(2)) * 1 + ['B', 'kB', 'MB', 'GB', 'TB'][i];
}

export const lerp = (start: number, end: number, amt: number) => {
    return (1 - amt) * start + amt * end;
};

export const persistentStore = (key:string, initialValue:any) => {
    const savedValue = JSON.parse(localStorage.getItem(key) as string);
    const data = savedValue !== null ? savedValue : initialValue;  
    const store = writable(data);
    
    store.subscribe(value => {
        localStorage.setItem(key, JSON.stringify(value));
    });
    
    return store;
}