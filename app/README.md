# create-svelte

Everything you need to build a Svelte project, powered by [`create-svelte`](https://github.com/sveltejs/kit/tree/main/packages/create-svelte).

## Creating a project

If you're seeing this, you've probably already done this step. Congrats!

```bash
# create a new project in the current directory
npx sv create
```

## Developing

Once you've created your project, follow these steps:

1: Delete package-lock.json
2: Check `git status`. If you see any changes other than package-lock.json or favicon.ico, run the command `git restore ./` (See below)
3: Run `npm install` or `pnpm install` or `yarn` to install the dependencies
4: Run `npm run build` to build the project

Running `git status` should show:

[![example.png](https://i.postimg.cc/yddM3hH3/example.png)](https://postimg.cc/7CFsp2bq)

You can preview the production build with `npm run preview`.

> To deploy your app, you may need to install an [adapter](https://kit.svelte.dev/docs/adapters) for your target environment.
