export const prerender = true
export const ssr = false

const registerFetchIntercept = async () => {
  const { fetch: originalFetch } = window
  const fileService = (await import('$lib/services/file-service')).default
  window.fetch = async (resource, config) => {
    const url = resource instanceof Request ? resource.url : resource.toString()
    const file = await fileService?.getFile(url)
    return file?.isOk() ? new Response(file.inner) : originalFetch(resource, config)
  }
}

export const load = async () => {
  await registerFetchIntercept()
  return {
    title: 'Spot micro controller',
    github: 'runeharlyk/SpotMicroESP32-Leika',
    app_name: 'Spot Micro Controller',
    copyright: '2025 Rune Harlyk'
  }
}
