import { expect, test } from '@playwright/test';

test('has title', async ({ page }) => {
	await page.goto('/');
	await page.route('**/api/features', (route) =>
		route.fulfill({
			status: 200,
			body: JSON.stringify({})
		})
	);

	await expect(page).toHaveTitle(/Spot micro controller/);
});

test('index page has expected h1', async ({ page }) => {
	await page.goto('/');
    await page.route('**/api/features', (route) =>
			route.fulfill({
				status: 200,
				body: JSON.stringify({})
			})
		);
	await expect(page.getByRole('heading', { name: 'Spot micro controller' }).first()).toBeVisible();
});
