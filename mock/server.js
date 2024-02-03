import express from "express";
import cors from "cors";
import Kinematic from "./kinematic.js";
import { WebSocketServer } from "ws";

const app = express();
const kinematic = new Kinematic();
const wss = new WebSocketServer({ port: 8080 });

app.use(cors());
app.use(express.json());

const port = 3000;
const subscriptions = {};

const randomFloatFromInterval = (min, max) =>
  Math.floor((Math.random() * (max - min + 1) + min) * 100) / 100;

const radToDeg = (val) => val * (180 / Math.PI);
const degToRad = (val) => val * (Math.PI / 180);

function createNewClientState() {
  return {
    model: JSON.parse(JSON.stringify(model)),
    settings: JSON.parse(JSON.stringify(settings)),
    logs: [...logs],
    subscriptions: {},
  };
}

function subscribeClientToCategory(ws, category) {
  if (!subscriptions[category]) {
    subscriptions[category] = new Set();
  }
  subscriptions[category].add(ws);
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
  "Booting up",
  "Starting webserver",
  "Loading setting",
  "Connected to Rune private network",
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

const updateBodyState = (angles, position) => {
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

wss.on("connection", (ws) => {
  const clientState = createNewClientState();
  ws.clientState = clientState;
  ws.on("error", console.error);

  ws.on("message", (message) => {
    let data = message;
    try {
      data = JSON.parse(message);
    } catch (error) {
      return;
    }
    switch (data.type) {
      case "subscribe":
        subscribeClientToCategory(ws, data.category);
        break;
      case "unsubscribe":
        unsubscribeClientFromCategory(ws, data.category);
        break;
      case "sensor/battery":
        ws.send({ type: "battery", battery: JSON.stringify(updateBattery()) });
        break;
      case "sensor/mpu":
        ws.send({ type: "battery", mpu: JSON.stringify(updateMpu()) });
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
              angles: ws.clientState.model.servos.angles,
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
              angles: ws.clientState.model.servos.angles,
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
              angles: updateBodyState(data.angles, data.position),
            })
          );
        } else {
          ws.send(JSON.stringify({ angles: model.servos.angles }));
        }
        break;
      case "system/log":
        ws.send(JSON.stringify(logs));
        break;
      case "system/info":
        ws.send(JSON.stringify(updateSystem()));
        break;
      case "system/settings":
        if (data.settings) {
          Object.entries(data.settings).forEach(
            ([key, value]) => (settings[key] = value)
          );
          ws.send(JSON.stringify(settings));
        } else {
          ws.send(JSON.stringify(settings));
        }
        break;
      case "system/stop":
        model.running = false;
        ws.send(JSON.stringify(settings));
        break;
      default:
        ws.send(JSON.stringify({ error: "Unknown request type" }));
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
  res.send(updateBodyState(req.body.angles, req.body.position));
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
app.get("/", function (req, res) {
  res.status(200).sendFile(`/`, { root: path });
});
app.get("*.*", express.static(path, { maxAge: "1y" }));
app.listen(port, () => console.log(`Open at http://localhost:${port}`));
