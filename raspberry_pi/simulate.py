from os import sys

from src.spot import Spot
from src.IMU.IMU import IMU
from src.Kinematics.SpotKinematics import SpotModel
from src.camera_server import StreamingServerThread
from src.Camera.WebCamera import WebCamera

sys.path.append('../')

from simulation.simulator import Simulator
from simulation.GymEnvs.spot_bezier_env import spotBezierEnv

from simulation.sensors.camera import PyBulletCamera

camera = PyBulletCamera()

env = spotBezierEnv(
    render=True,
    on_rack=False,
    height_field=False,
    draw_foot_path=False,
    urdf_root="..\simulation\pybullet_data"
)
kinematics = SpotModel()
imu = IMU()
camera = PyBulletCamera() # WebCamera()
spot = Spot(kinematics, imu)

server = StreamingServerThread(camera)
sim = Simulator(env, spot)

server.start()

while True:
    camera.update_view_matrix(sim.env.spot.quadruped)
    camera.get_image()
    sim.step(0.01)