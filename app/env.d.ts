declare module "app-env" {
  interface ENV {
    VITE_USE_HOST_NAME: boolean;
  }

  const appEnv: ENV;
  export default appEnv;
}
