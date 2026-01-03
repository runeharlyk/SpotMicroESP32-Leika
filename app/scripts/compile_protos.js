#!/usr/bin/env node
import { execSync } from 'child_process'
import path from 'path'
import os from 'os'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

const isWindows = os.platform() === 'win32'
const projectRoot = path.resolve(__dirname, '..')
const platformSharedDir = path.resolve(projectRoot, '..', 'platform_shared')
const outputDir = path.resolve(projectRoot, 'src', 'lib', 'platform_shared')

const pluginPath =
    isWindows ?
        path.join(projectRoot, 'node_modules', '.bin', 'protoc-gen-ts_proto.cmd')
    :   path.join(projectRoot, 'node_modules', '.bin', 'protoc-gen-ts_proto')

const protoFiles = ['websocket_message.proto', 'rest_message.proto']

const tsProtoOpts = ['useExactTypes=false', 'outputExtensions=true', 'outputSchema=true'].join(',')

const cmd = [
    'protoc',
    `--plugin=protoc-gen-ts_proto=${pluginPath}`,
    `--ts_proto_out=${outputDir}`,
    `--ts_proto_opt=${tsProtoOpts}`,
    `-I${platformSharedDir}`,
    ...protoFiles.map(f => path.join(platformSharedDir, f))
].join(' ')

console.log('Compiling protos...')
console.log(`  Platform: ${os.platform()}`)
console.log(`  Output: ${outputDir}`)

try {
    execSync(cmd, { stdio: 'inherit', cwd: projectRoot })
    console.log('Proto compilation complete!')
} catch (error) {
    console.error('Proto compilation failed!', error)
    process.exit(1)
}
