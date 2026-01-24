import { get } from 'svelte/store'
import { Err, Ok, type Result } from './utilities'
import { apiLocation } from './stores/location-store'
import type { MessageFns } from './platform_shared/filesystem'
import { Request, Response } from './platform_shared/api'
import { BinaryWriter } from '@bufbuild/protobuf/wire'

export const api = {
    get<TResponse>(endpoint: string, params?: RequestInit) {
        return sendRequest<TResponse>(endpoint, 'GET', null, params)
    },

    post<TResponse>(endpoint: string, data?: unknown) {
        return sendRequest<TResponse>(endpoint, 'POST', data)
    },

    post_proto<TResponse>(endpoint: string, data: Request) {
        return sendRequest<TResponse>(endpoint, 'POST', Request.encode(data))
    },

    put<TResponse>(endpoint: string, data?: unknown) {
        return sendRequest<TResponse>(endpoint, 'PUT', data)
    },

    remove<TResponse>(endpoint: string) {
        return sendRequest<TResponse>(endpoint, 'DELETE')
    }
}

async function sendRequest<TResponse>(
    endpoint: string,
    method: string,
    data?: unknown,
    params?: RequestInit
): Promise<Result<TResponse, Error>> {
    endpoint = resolveUrl(endpoint)

    const isProtobuf = data instanceof BinaryWriter
    const body = data !== null && typeof data !== 'undefined'
        ? (isProtobuf ? data.finish() : JSON.stringify(data))
        : undefined

    const request = {
        ...params,
        method,
        body,
        headers: {
            ...params?.headers,
            Authorization: 'Basic',
            'Content-Type': isProtobuf ? 'application/x-protobuf' : 'application/json'
        }
    }

    let response

    try {
        response = await fetch(endpoint, request)
    } catch {
        return Err.new(new Error(), 'An error has occurred')
    }

    const isResponseOk = response.status >= 200 && response.status < 400
    if (!isResponseOk) {
        if (response.status === 401) {
            return Err.new(new ApiError(response), 'User was not authorized')
        }
        return Err.new(new ApiError(response), 'An error has occurred')
    }

    const contentType = response.headers.get('Content-Type') ?? response.headers.get('Content-Type')
    if (contentType && contentType.includes('application/json')) {
        const data = await response.json()
        return Ok.new(data as TResponse)
    } else if (contentType && contentType.includes('application/x-protobuf')) {
        let data: Response = Response.decode(await response.bytes());
        return Ok.new(data as TResponse)
    } else {
        // Handle empty object as response
        return Ok.new(null as TResponse)
    }
}

function resolveUrl(url: string): string {
    if (url.startsWith('http') || !get(apiLocation)) return url
    const protocol = window.location.protocol
    return `${protocol}//${get(apiLocation)}${url.startsWith('/') ? '' : '/'}${url}`
}

export class ApiError extends Error {
    constructor(public readonly response: Response) {
        super(`${response.status}`)
    }
}
