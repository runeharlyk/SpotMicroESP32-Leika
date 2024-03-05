# SCRIPT FOR RUNNING THE RPI ROBOT

import time
from src.spot import Spot
from src.kinematics.SpotKinematics import SpotModel
from src.camera.WebCamera import WebCamera
from src.imu.IMU import IMU
from src.hardware_interface.HardwareInterface import HardwareInterface
from src.controller_interface.WebsocketController import WebsocketController
from src.controller_interface.SharedControllerState import SharedState, shared_state


def main():
    kinematics = SpotModel()
    camera = WebCamera()
    imu = IMU()
    hardware_interface = HardwareInterface()
    # shared_controller_state = SharedState()
    controller_interface = WebsocketController() # shared_controller_state)

    spot = Spot(kinematics, camera, imu, hardware_interface, controller_interface)

    spot.start()

    try:
        while True:
            spot.run(shared_state.get_latest_state().__dict__)
            time.sleep(1)
    except KeyboardInterrupt:
        spot.stop()

if __name__ == '__main__':
    main()