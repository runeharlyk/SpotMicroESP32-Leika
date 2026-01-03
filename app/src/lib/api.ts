import { get } from 'svelte/store'
import { Err, Ok, type Result } from './utilities'
import { apiLocation } from './stores/location-store'
import type { MessageFns } from './platform_shared/message'
import { BinaryReader } from '@bufbuild/protobuf/wire'

export const api = {
    get<TResponse>(
        endpoint: string,
        responseType: MessageFns<TResponse>,
        params?: RequestInit
    ): Promise<Result<TResponse, Error>> {
        return sendProtoRequest<undefined, TResponse>(endpoint, 'GET', undefined, undefined, responseType, params)
    },

    getEmpty(endpoint: string, params?: RequestInit): Promise<Result<void, Error>> {
        return sendEmptyRequest(endpoint, 'GET', params)
    },

    post<TRequest, TResponse>(
        endpoint: string,
        data: TRequest,
        requestType: MessageFns<TRequest>,
        responseType: MessageFns<TResponse>
    ): Promise<Result<TResponse, Error>> {
        return sendProtoRequest<TRequest, TResponse>(endpoint, 'POST', data, requestType, responseType)
    },

    postNoResponse<TRequest>(
        endpoint: string,
        data: TRequest,
        requestType: MessageFns<TRequest>
    ): Promise<Result<void, Error>> {
        return sendProtoRequestNoResponse<TRequest>(endpoint, 'POST', data, requestType)
    },

    postEmpty(endpoint: string): Promise<Result<void, Error>> {
        return sendEmptyRequest(endpoint, 'POST')
    },

    postFormData(endpoint: string, formData: FormData): Promise<Result<void, Error>> {
        return sendFormDataRequest(endpoint, formData)
    },

    remove(endpoint: string): Promise<Result<void, Error>> {
        return sendEmptyRequest(endpoint, 'DELETE')
    }
}

export const jsonApi = {
    get<TResponse>(endpoint: string, params?: RequestInit): Promise<Result<TResponse, Error>> {
        return sendJsonRequest<TResponse>(endpoint, 'GET', null, params)
    },

    post<TResponse>(endpoint: string, data?: unknown): Promise<Result<TResponse, Error>> {
        return sendJsonRequest<TResponse>(endpoint, 'POST', data)
    }
}

async function sendJsonRequest<TResponse>(
    endpoint: string,
    method: string,
    data?: unknown,
    params?: RequestInit
): Promise<Result<TResponse, Error>> {
    const body = data !== null && typeof data !== 'undefined' ? JSON.stringify(data) : undefined

    const request: RequestInit = {
        ...params,
        method,
        body,
        headers: {
            ...params?.headers,
            'Content-Type': 'application/json'
        }
    }

    let response: Response
    try {
        response = await fetch(endpoint, request)
    } catch {
        return Err.new(new Error(), 'An error has occurred')
    }

    const isResponseOk = response.status >= 200 && response.status < 400
    if (!isResponseOk) {
        return Err.new(new ApiError(response), 'An error has occurred')
    }

    const contentType = response.headers.get('Content-Type') ?? ''
    if (contentType.includes('application/json')) {
        const data = await response.json()
        return Ok.new(data as TResponse)
    } else {
        return Ok.new(null as TResponse)
    }
}

async function sendProtoRequest<TRequest, TResponse>(
    endpoint: string,
    method: string,
    data: TRequest | undefined,
    requestType: MessageFns<TRequest> | undefined,
    responseType: MessageFns<TResponse>,
    params?: RequestInit
): Promise<Result<TResponse, Error>> {
    endpoint = resolveUrl(endpoint)

    let body: Uint8Array | undefined
    if (data !== undefined && requestType !== undefined) {
        body = requestType.encode(data).finish()
    }

    const request: RequestInit = {
        ...params,
        method,
        body: body as BodyInit | undefined,
        headers: {
            ...params?.headers,
            Authorization: 'Basic',
            'Content-Type': 'application/x-protobuf',
            Accept: 'application/x-protobuf'
        }
    }

    let response: Response
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

    const arrayBuffer = await response.arrayBuffer()
    if (arrayBuffer.byteLength === 0) {
        return Ok.new(responseType.create({} as Parameters<typeof responseType.create>[0]) as TResponse)
    }

    try {
        const decoded = responseType.decode(new BinaryReader(new Uint8Array(arrayBuffer)))
        return Ok.new(decoded)
    } catch (e) {
        return Err.new(e instanceof Error ? e : new Error(String(e)), 'Failed to decode response')
    }
}

async function sendProtoRequestNoResponse<TRequest>(
    endpoint: string,
    method: string,
    data: TRequest,
    requestType: MessageFns<TRequest>,
    params?: RequestInit
): Promise<Result<void, Error>> {
    endpoint = resolveUrl(endpoint)

    const body = requestType.encode(data).finish()

    const request: RequestInit = {
        ...params,
        method,
        body: body as BodyInit | undefined,
        headers: {
            ...params?.headers,
            Authorization: 'Basic',
            'Content-Type': 'application/x-protobuf'
        }
    }

    let response: Response
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

    return Ok.new(undefined)
}

async function sendEmptyRequest(
    endpoint: string,
    method: string,
    params?: RequestInit
): Promise<Result<void, Error>> {
    endpoint = resolveUrl(endpoint)

    const request: RequestInit = {
        ...params,
        method,
        headers: {
            ...params?.headers,
            Authorization: 'Basic'
        }
    }

    let response: Response
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

    return Ok.new(undefined)
}

async function sendFormDataRequest(
    endpoint: string,
    formData: FormData
): Promise<Result<void, Error>> {
    endpoint = resolveUrl(endpoint)

    const request: RequestInit = {
        method: 'POST',
        body: formData,
        headers: {
            Authorization: 'Basic'
        }
    }

    let response: Response
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

    return Ok.new(undefined)
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
