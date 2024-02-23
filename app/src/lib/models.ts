export type angles = number[] | Int16Array;

type AnglesData = {
	type: 'angles';
	data: angles;
};

type LogData = {
	type: 'log';
	data: string;
};

export type WebSocketJsonMsg = AnglesData | LogData;
