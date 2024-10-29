import { get } from 'svelte/store';
import { Err, Ok, type Result } from './utilities';
import { location } from './stores';

export namespace api {
    export function get<TResponse>(endpoint: string, params?: RequestInit) {
        return sendRequest<TResponse>(endpoint, 'GET', null, params);
    }

    export function post<TResponse>(endpoint: string, data?: unknown) {
        return sendRequest<TResponse>(endpoint, 'POST', data);
    }

    export function put<TResponse>(endpoint: string, data?: unknown) {
        return sendRequest<TResponse>(endpoint, 'PUT', data);
    }

    export function remove<TResponse>(endpoint: string) {
        return sendRequest<TResponse>(endpoint, 'DELETE');
    }
}

async function sendRequest<TResponse>(
    endpoint: string,
    method: string,
    data?: unknown,
    params?: RequestInit
): Promise<Result<TResponse, Error>> {
    endpoint = resolveUrl(endpoint);
    const body = data !== null && typeof data !== 'undefined' ? JSON.stringify(data) : undefined;

    const request = {
        ...params,
        method,
        body,
        headers: {
            ...params?.headers,
            Authorization: 'Basic',
            'Content-Type': 'application/json'
        }
    };

    let response;

    try {
        response = await fetch(endpoint, request);
    } catch (error) {
        return Err.new(new Error(), 'An error has occurred');
    }

    const isResponseOk = response.status >= 200 && response.status < 400;
    if (!isResponseOk) {
        if (response.status === 401) {
            return Err.new(new ApiError(response), 'User was not authorized');
        }
        return Err.new(new ApiError(response), 'An error has occurred');
    }

    const contentType =
        response.headers.get('Content-Type') ?? response.headers.get('Content-Type');
    if (contentType && contentType.includes('application/json')) {
        const data = await response.json();
        return Ok.new(data as TResponse);
    } else {
        // Handle empty object as response
        return Ok.new(null as TResponse);
    }
}

function resolveUrl(url: string): string {
    if (url.startsWith('http') || !get(location)) return url;
    const protocol = window.location.protocol;
    return `${protocol}//${get(location)}${url.startsWith('/') ? '' : '/'}${url}`;
}

export class ApiError extends Error {
    constructor(public readonly response: Response) {
        super(`${response.status}`);
    }
}
