const forWeb = import.meta.env.MODE === "WEB"
const mock = import.meta.env.MODE === "MOCK"

const location = mock ? `${window.location.hostname}:2096` : "leika.local"

export const socketLocation = forWeb ? `wss://${window.location.hostname}:2096` : `ws://${location}` 

export default location;