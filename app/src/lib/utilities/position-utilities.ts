class SunCalculator {
	calculateSunElevation(lat: number = 55, lon: number = 12) {
		const now = new Date();
		const JD = this.getJulianDate(now);
		const solarDec = this.getSolarDeclination(JD);
		const solarTime = this.getSolarTime(now, lon);

		const hourAngle = (solarTime - 12) * 15;
		const elevation = Math.asin(
			Math.sin(this.degToRad(lat)) * Math.sin(solarDec) +
				Math.cos(this.degToRad(lat)) * Math.cos(solarDec) * Math.cos(this.degToRad(hourAngle))
		);

		return this.radToDeg(elevation);
	}

	getJulianDate(date: Date) {
		const Y = date.getUTCFullYear();
		const M = date.getUTCMonth() + 1;
		const D =
			date.getUTCDate() +
			date.getUTCHours() / 24 +
			date.getUTCMinutes() / 1440 +
			date.getUTCSeconds() / 86400;
		const A = Math.floor((14 - M) / 12);
		const Y1 = Y + 4800 - A;
		const M1 = M + 12 * A - 3;
		return (
			D +
			Math.floor((153 * M1 + 2) / 5) +
			365 * Y1 +
			Math.floor(Y1 / 4) -
			Math.floor(Y1 / 100) +
			Math.floor(Y1 / 400) -
			32045
		);
	}

	getSolarDeclination(JulianDate: number) {
		const n = JulianDate - 2451545;
		const L = (280.46 + 0.9856474 * n) % 360;
		const g = this.degToRad((357.528 + 0.9856003 * n) % 360);
		const lambda = this.degToRad(L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g));
		return Math.asin(Math.sin(lambda) * Math.sin(this.degToRad(23.44)));
	}

	getSolarTime(date: Date, lon: number) {
		const EoT = this.getEquationOfTime(date);
		const offset = date.getTimezoneOffset() / 60;
		const standardMeridian = Math.round(lon / 15) * 15;
		const solarTime =
			date.getUTCHours() +
			(date.getUTCMinutes() + (4 * (standardMeridian - lon) + EoT)) / 60 -
			offset;
		return (solarTime + 24) % 24;
	}

	getEquationOfTime(date: Date) {
		const JD = this.getJulianDate(date);
		const n = JD - 2451545;
		const g = this.degToRad((357.528 + 0.9856003 * n) % 360);
		const q = this.degToRad((280.46 + 0.9856474 * n) % 360);
		return (
			4 *
			this.radToDeg(
				0.000075 +
					0.001868 * Math.cos(q) -
					0.032077 * Math.sin(g) -
					0.014615 * Math.cos(2 * q) -
					0.040849 * Math.sin(2 * g)
			)
		);
	}

	degToRad(deg: number) {
		return deg * (Math.PI / 180);
	}

	radToDeg(rad: number) {
		return rad * (180 / Math.PI);
	}
}

export const sunCalculator = new SunCalculator();
