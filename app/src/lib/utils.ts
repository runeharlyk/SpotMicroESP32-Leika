import { writable } from 'svelte/store';

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