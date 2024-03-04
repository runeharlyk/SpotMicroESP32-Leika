import express from "express";
import cors from "cors";
import Kinematic from "./kinematic.js";
import { WebSocketServer } from "ws";

const app = express();
const kinematic = new Kinematic();
const wss = new WebSocketServer({ port: 2096 });

app.use(cors());
app.use(express.json());

const port = 3000;
const subscriptions = {};

const randomFloatFromInterval = (min, max) =>
  Math.floor((Math.random() * (max - min + 1) + min) * 100) / 100;

const radToDeg = (val) => val * (180 / Math.PI);
const degToRad = (val) => val * (Math.PI / 180);

const lerp = (start, end, amt) => {
  return (1 - amt) * start + amt * end;
};

function createNewClientState() {
  return {
    model: JSON.parse(JSON.stringify(model)),
    settings: JSON.parse(JSON.stringify(settings)),
    logs: JSON.parse(JSON.stringify(logs)),
    subscriptions: {},
    mode: "idle",
    controller: {
      stop: 0,
      lx: 0,
      ly: 0,
      rx: 0,
      ry: 0,
      h: 70,
      s: 0,
    },
  };
}

const unsubscribeClientFromCategory = (ws, category) => {
  if (!subscriptions[category]) return;
  subscriptions[category].delete(ws);
  if (subscriptions[category].size === 0) {
    delete subscriptions[category].size;
  }
};

const sendUpdateToSubscribers = (category, data) => {
  if (subscriptions[category]) {
    const message = JSON.stringify(data);
    for (const client of subscriptions[category]) {
      client.send(message);
    }
  }
};

if (!Array.prototype.last) {
  Array.prototype.last = function () {
    return this[this.length - 1];
  };
}

const model = {
  battery: {
    voltage: randomFloatFromInterval(7.6, 8.2),
    ampere: randomFloatFromInterval(0.2, 3),
    power_button: false,
  },
  servos: {
    angles: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    dir: [-1, -1, -1, 1, -1, -1, -1, -1, -1, 1, -1, -1],
    on: true,
  },
  mpu: {
    x: 0,
    y: 0,
    z: 0,
    heading: 240,
    temperature: 21,
  },
  display: [[]],
  distance_sensors: {
    left: 22,
    right: 23,
  },
  appTime: 1123321,
  connectivity: {
    ssid: "best network",
    ip: "192.168.0.118",
    mDNS: "leika.local",
    rssi: 100,
  },
  running: true,
  mode: "stand",
  rotation: [0, 0, 0],
  position: [0, 0, 0],
};

const settings = {
  useMetric: true,
  name: "Leika",
  ssid: "Rune private network",
  pass: "12345678",
  ap: "Leika",
  apPass: "12345678",
  apChannel: 1,
};

const logs = [
  "[2023-02-05 10:00:00] [verbose] Booting up",
  "[2023-02-05 10:00:10] [verbose] Starting webserver",
  "[2023-02-05 10:00:20] [verbose] Loading setting",
  "[2023-02-05 10:00:30] [verbose] Connected to Rune private network",
];

const system = {
  HeapSize: 400000,
  HeapFree: 0,
  HeapMin: 0,
  DmaHeapSize: 0,
  DmaHeapFree: 0,
  DmaHeapMin: 0,
  PsramSize: 400000,
  PsramFree: 0,
  PsramMin: 0,
  ChipModel: 0,
  ChipRevision: 0,
  ChipCores: 2,
  CpuFreqMHz: 80,
  SketchSize: 0,
  FreeSketchSpace: 10200,
  FlashChipSize: 0,
  CpuUsed: 0,
  CpuUsedCore0: 0,
  CpuUsedCore1: 0,
  arduinoVersion: "3.2.1",
};

const updateBattery = () => {
  model.battery.voltage = randomFloatFromInterval(7.6, 8.2);
  model.battery.ampere = randomFloatFromInterval(0.2, 3);
  return model.battery;
};

const updateMpu = () => {
  model.mpu.x = randomFloatFromInterval(0, 1);
  model.mpu.y = randomFloatFromInterval(0, 1);
  model.mpu.z = randomFloatFromInterval(0, 1);
  model.mpu.temperature = randomFloatFromInterval(20, 22);
  return model.mpu;
};

const updateDistances = () => {
  model.distance_sensors = {
    left: randomFloatFromInterval(10, 220),
    right: randomFloatFromInterval(10, 220),
  };
  return model.distance_sensors;
};

const updateDistance = (position) => {
  model.distance_sensors[position] = randomFloatFromInterval(10, 220);
  return model.distance_sensors[position];
};

const updateSystem = () => {
  system.CpuUsedCore0 = randomFloatFromInterval(0, 100);
  system.CpuUsedCore1 = randomFloatFromInterval(0, 100);
  system.CpuUsed =
    Math.floor((system.CpuUsedCore0 + system.CpuUsedCore1) / 0.02) / 100;
  system.HeapFree = randomFloatFromInterval(0, 20000);
  system.HeapMin = randomFloatFromInterval(0, 20000);
  return system;
};

const updateBodyState = (model, angles, position) => {
  const Lp = [
    [100, -100, 100, 1],
    [100, -100, -100, 1],
    [-100, -100, 100, 1],
    [-100, -100, -100, 1],
  ];

  model.servos.angles = kinematic
    .calcIK(
      Lp,
      angles.map((x) => degToRad(x)),
      position
    )
    .flat()
    .map((x, i) => radToDeg(x * model.servos.dir[i]));
  return model.servos.angles;
};

const updateAngle = (id, angle) => {
  model.servos.angles[id] = angle;
  return model.servos.angles;
};

const updateAngles = (angles) => {
  model.servos.angles = angles;
  return model.servos.angles;
};

const bufferToController = (buffer) => {
  return {
    command: buffer[0],
    stop: buffer[1],
    lx: buffer[2],
    ly: buffer[3],
    rx: buffer[4],
    ry: buffer[5],
    h: buffer[6],
    s: buffer[7],
  };
};

const unpackMessageBuffer = (data) => {
  return {
    angles: [0, data.rx / 4, data.ry / 4],
    position: [data.ly / 2, 70, data.lx / 2],
  };
};

const rest_stance = {
  rotation: [0, 0, 0],
  position: [0, 10, 0],
};

const idle = () => {};

const rest = (client) => {
  for (let i = 0; i < 3; i++) {
    client.clientState.model.position[i] = lerp(
      client.clientState.model.position[i],
      rest_stance.position[i],
      0.01
    );
    client.clientState.model.rotation[i] = lerp(
      client.clientState.model.rotation[i],
      rest_stance.rotation[i],
      0.01
    );
  }
  client.send(
    JSON.stringify({
      type: "angles",
      data: updateBodyState(
        client.clientState.model,
        client.clientState.model.rotation,
        client.clientState.model.position
      ),
    })
  );
};

const stand = (client) => {
  if (!client.clientState.model.running) return;
  const data = unpackMessageBuffer(client.clientState.controller);
  client.send(
    JSON.stringify({
      type: "angles",
      data: updateBodyState(
        client.clientState.model,
        data.angles,
        data.position
      ),
    })
  );
};

// https://www.hindawi.com/journals/cin/2016/9853070/

const step = (model, controller, tick) => {
  const arc_height = controller.h;
  const speed = (controller.s + 128) / 255 / 4;

  const T_stride_s = 500 / 1000;
  const overlay = 10 / 100;

  const T_stance_s = T_stride_s * (0.5 + overlay);
  const T_swing_s = T_stride_s * (0.5 - overlay);

  const x = Math.abs((tick % 200) - 100);

  const y1 = Math.min(
    Math.max(-arc_height * Math.sin(-speed * tick) - 150, -200),
    -100
  );
  const y2 = Math.min(
    Math.max(-arc_height * Math.sin(-speed * tick + Math.PI) - 150, -200),
    -100
  );
  const Lp = [
    // -50  is minimum
    [100, y1, 100, 1],
    [100, y2, -100, 1],
    [-65, y2, 100, 1],
    [-65, y1, -100, 1],
  ];

  model.servos.angles = kinematic
    .calcIK(
      Lp,
      model.rotation.map((x) => degToRad(x)),
      model.position
    )
    .flat()
    .map((x, i) => radToDeg(x * model.servos.dir[i]));
  return model.servos.angles;
};

const walk = (client) => {
  const angles = step(
    client.clientState.model,
    client.clientState.controller,
    client.tick
  );
  client.send(JSON.stringify({ type: "angles", data: angles }));
};

const start_dynamics = (client) => {
  client.tick = 0;
  client.clientState.mode = "idle";
  client.clientState.next_mode = "walk";
  const modes = { rest, idle, stand, walk };
  client.id = setInterval(() => {
    client.tick += 1;

    if (client.clientState.mode !== client.clientState.next_mode) {
      // Transition
      client.clientState.mode = client.clientState.next_mode;
    } else {
      modes[client.clientState.mode](client);
    }
  }, 10);
};

const handelController = (ws, buffer) => {
  const controllerData = bufferToController(new Int8Array(buffer));
  ws.clientState.controller = controllerData;
  if (controllerData.stop) {
    ws.clientState.model.running = false;
    ws.clientState.logs.push("[2024-02-05 19:10:00] [Warning] STOPPING SERVOS");
    ws.send(JSON.stringify({ type: "log", data: ws.clientState.logs.last() }));
    return;
  }
};

const handleBufferMessage = (ws, buffer) => {
  if (buffer.length === 8) {
    handelController(ws, buffer);
  }
};

const handleJsonMessage = (ws, data) => {
  switch (data.type) {
    case "mode":
      ws.clientState.next_mode = data.data;
      //   ws.send({ type: "battery", data: JSON.stringify(updateBattery()) });
      break;
    case "sensor/battery":
      ws.send({ type: "battery", data: JSON.stringify(updateBattery()) });
      break;
    case "sensor/mpu":
      ws.send({ type: "battery", data: JSON.stringify(updateMpu()) });
      break;
    case "sensor/distances":
      ws.send(JSON.stringify(updateDistances()));
      break;
    case "sensor/distance":
      ws.send(JSON.stringify({ distance: updateDistance(data.position) }));
      break;
    case "kinematic/angle":
      if (data.angle && data.id) {
        ws.clientState.model.servos.angles[data.id] = data.angle;
        ws.send(
          JSON.stringify({
            type: "angles",
            data: ws.clientState.model.servos.angles,
          })
        );
      } else {
        ws.send(JSON.stringify(updateAngle(data.id, data.angle)));
      }
      break;
    case "kinematic/angles":
      if (data.angles) {
        ws.clientState.model.servos.angles = data.angles;
        ws.send(
          JSON.stringify({
            type: "angles",
            data: ws.clientState.model.servos.angles,
          })
        );
      } else {
        ws.send(JSON.stringify(updateAngles(data.angles)));
      }
      break;
    case "kinematic/bodystate":
      if (data.angles) {
        ws.send(
          JSON.stringify({
            type: "angles",
            data: updateBodyState(
              ws.clientState.model,
              data.angles,
              data.position
            ),
          })
        );
      } else {
        ws.send(JSON.stringify({ angles: model.servos.angles }));
      }
      break;
    case "system/logs":
      ws.send(JSON.stringify({ type: "logs", data: ws.clientState.logs }));
      break;
    case "system/info":
      ws.send(JSON.stringify({ type: "info", data: updateSystem() }));
      break;
    case "system/settings":
      if (data.settings) {
        Object.entries(data.settings).forEach(
          ([key, value]) => (ws.clientState.settings[key] = value)
        );
        ws.send(JSON.stringify(ws.clientState.settings));
      } else {
        ws.send(
          JSON.stringify({
            type: "settings",
            settings: ws.clientState.settings,
          })
        );
      }
      break;
    case "system/stop":
      ws.clientState.model.running = false;
      ws.clientState.logs.push(
        "[2024-02-05 19:10:00] [Warning] STOPPING SERVOS"
      );
      ws.send(
        JSON.stringify({ type: "log", data: ws.clientState.logs.last() })
      );
      break;
    default:
      ws.send(JSON.stringify({ error: "Unknown request type" }));
  }
};

wss.on("connection", (ws) => {
  const clientState = createNewClientState();
  ws.clientState = clientState;
  start_dynamics(ws);
  ws.on("error", console.error);

  ws.on("message", (message) => {
    let data = message;
    try {
      data = JSON.parse(message);
      handleJsonMessage(ws, data);
    } catch (error) {
      handleBufferMessage(ws, message);
    }
  });

  ws.on("close", () => {
    for (const category in subscriptions) {
      unsubscribeClientFromCategory(ws, category);
    }
  });
});

app.get("/sensor/battery", (req, res) => res.send(updateBattery()));
app.get("/sensor/mpu", (req, res) => res.send(updateMpu()));
app.get("/sensor/distances", (req, res) => res.send(updateDistances()));
app.get("/sensor/distance/:position", (req, res) =>
  res.send({ distance: updateDistance(req.params.position) })
);

// ----------------------------------------------------------- //

app.post("/kinematic/angle/:id", (req, res) =>
  res.send(updateAngle(req.params.id, req.body.angle))
);
app.post("/kinematic/angles/", (req, res) =>
  res.send(updateAngles(req.body.angles))
);
app.get("/kinematic/bodystate", (req, res) => res.send(model.servos.angles));
app.post("/kinematic/bodystate", (req, res) => {
  sendUpdateToSubscribers("angles", model.servos.angles);
  res.send(updateBodyState(model, req.body.angles, req.body.position));
});

// ----------------------------------------------------------- //

app.get("/system/log", (req, res) => res.send(logs));
app.get("/system/info", (req, res) => res.send(updateSystem()));
app.get("/system/settings", (req, res) => res.send(settings));
app.post("/system/settings", (req, res) => {
  Object.entries(req.body).forEach((x) => (settings[x[0]] = x[1]));
  res.send(settings);
});
app.post("/system/stop", (req, res) => {
  model.running = false;
  model.res.send(settings);
});
app.listen(port, () => console.log(`Open at http://localhost:${port}`));
