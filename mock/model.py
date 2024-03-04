import random


model = lambda: {
    "gait": {
        "step_length": 0,
        "yaw_rate": 0,
        "lateral_fraction": 0,
        "step_velocity": 0,
        "swing_period": 0,
        "clearance_height": 0,
        "penetration_depth": 0,
        "contacts": 0,
    },
    "transformation": {
        "world_position": [0, 0, 0],
        "position": [0, 0, 0],
        "rotation": [0, 0, 0],
        "world_feet_positions": {},
    },
    "sensors": {
        "mpu": {
            "x": 0,
            "y": 0,
            "z": 0,
        },
        "battery": {
            "voltage": round(random.uniform(7.6, 8.2), 2),
            "ampere": round(random.uniform(0.2, 3), 2),
        },
    },
    "logs": ["[2023-02-05 10:00:00] Booting up"],
    "settings": {"useMetric": True},
}
