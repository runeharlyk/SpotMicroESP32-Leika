from os import sys
import time

from src.spot import Spot
from src.kinematics.SpotKinematics import SpotModel
from src.camera.WebCamera import WebCamera
from src.imu.IMU import IMU
from src.controller_interface.WebsocketController import WebsocketController
from src.controller_interface.SharedControllerState import SharedState, shared_state

# from src.camera_server import StreamingServerThread

sys.path.append('../')

from simulation.simulator import Simulator
from simulation.GymEnvs.spot_bezier_env import spotBezierEnv
from simulation.sensors.camera import PyBulletCamera
from simulation.sensors.hardware_interface import PyBulletHardwareInterface

def main():
    env = spotBezierEnv(
        render=True,
        on_rack=False,
        height_field=False,
        draw_foot_path=False,
        urdf_root="..\simulation\pybullet_data"
    )

    kinematics = SpotModel()
    camera = WebCamera()
    camera = PyBulletCamera(env.spot.quadruped)
    imu = IMU()
    hardware_interface = PyBulletHardwareInterface(env)
    # shared_controller_state = SharedState()
    controller_interface = WebsocketController(shared_state)

    spot = Spot(
        kinematics, 
        camera, 
        imu, 
        hardware_interface, 
        controller_interface, 
        shared_state
    )

    simulator = Simulator(env, spot)

    spot.start()

    last_time = time.time()

    try:
        while True:
            contacts, done = simulator.step()
            spot.gait_state.contacts = contacts
            spot.run() # time.time() - last_time
            last_time = time.time()
            if done:
                break
    except KeyboardInterrupt:
        print("Program was interrupted")
    spot.stop()

if __name__ == '__main__':
    main()

