import { errorLogs, latestErrorLog } from '$lib/stores';
import type { Result } from '$lib/utilities';

class ResultService {
	public handleResult(result: Result<unknown, string>, tag?: string) {
		if (result.isErr()) {
			const errorLogEntry = { tag, message: result.inner, exception: result.exception };
			latestErrorLog.set(errorLogEntry);
			errorLogs.update((entries) => {
				entries.push(errorLogEntry);
				return entries;
			});
		}

		return result;
	}
}

export default new ResultService();
