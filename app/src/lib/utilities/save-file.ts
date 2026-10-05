/** Hands `data` to the browser as a download named `filename`. */
export function saveFile(data: BlobPart, filename: string, type = 'application/octet-stream') {
    const url = URL.createObjectURL(new Blob([data], { type }))
    const link = document.createElement('a')
    link.href = url
    link.download = filename
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    URL.revokeObjectURL(url)
}
