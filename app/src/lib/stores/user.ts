import { goto } from '$app/navigation';
import { jwtDecode } from 'jwt-decode';
import { persistentStore } from '$lib/utilities';

export type UserProfile = {
	username: string;
	admin: boolean;
	bearer_token: string;
};

type DecodedJWT = Omit<UserProfile, 'bearer_token'>;

const emptyUser: UserProfile = {
	username: '',
	admin: false,
	bearer_token: ''
};

function createUserStore() {
	const store = persistentStore<UserProfile>('user', emptyUser);

	return {
		subscribe: store.subscribe,
		init: (access_token: string) => {
			const decoded: DecodedJWT = jwtDecode(access_token);
			const userProfile: UserProfile = {
				bearer_token: access_token,
				username: decoded.username,
				admin: decoded.admin
			};
			store.set(userProfile);
		},
		invalidate: () => {
			store.set(emptyUser);
			goto('/');
		}
	};
}

export const user = createUserStore();
