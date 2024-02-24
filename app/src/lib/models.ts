export type vector = { x: number; y: number };

export interface ControllerInput {
	left: vector;
	right: vector;
	height: number;
	speed: number;
}

export type angles = number[] | Int16Array;

export type AnglesData = {
	type: 'angles';
	data: angles;
};

export type LogData = {
	type: 'log';
	data: string;
};

export type WebSocketJsonMsg = AnglesData | LogData;
