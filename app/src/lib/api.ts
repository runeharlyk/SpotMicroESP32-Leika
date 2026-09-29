import { Ok, Result } from './utilities'
import { robotHttpUrl } from './stores/location-store'
import { Request, Response as ProtoResponse } from './platform_shared/api'
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
    }
}

async function sendRequest<TResponse>(
    endpoint: string,
    method: string,
    data?: unknown,
    params?: RequestInit
): Promise<Result<TResponse, Error>> {
    endpoint = robotHttpUrl(endpoint)

    const isProtobuf = data instanceof BinaryWriter
    const body =
        data !== null && typeof data !== 'undefined' ?
            isProtobuf ? data.finish()
            :   JSON.stringify(data)
        :   undefined

    const request = {
        ...params,
        method,
        body,
        headers: {
            ...params?.headers,
            'Content-Type': isProtobuf ? 'application/x-protobuf' : 'application/json'
        }
    }

    let response

    try {
        response = await fetch(endpoint, request)
    } catch (e) {
        return Result.err(new Error('Could not reach the robot', { cause: e }))
    }

    const isResponseOk = response.status >= 200 && response.status < 400
    if (!isResponseOk) return Result.err(new ApiError(response))

    const contentType = response.headers.get('Content-Type')
    if (contentType && contentType.includes('application/json')) {
        const data = await response.json()
        return Ok.new(data as TResponse)
    } else if (contentType && contentType.includes('application/x-protobuf')) {
        const data: ProtoResponse = ProtoResponse.decode(await response.bytes())
        return Ok.new(data as TResponse)
    } else {
        // Handle empty object as response
        return Ok.new(null as TResponse)
    }
}

export class ApiError extends Error {
    constructor(public readonly response: Response) {
        super(
            response.status === 401 ?
                'Not authorized'
            :   `Robot responded with HTTP ${response.status} ${response.statusText}`.trimEnd()
        )
    }
}
