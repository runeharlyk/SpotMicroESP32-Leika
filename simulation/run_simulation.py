from os import sys

sys.path.append('../')

from raspberry_pi.src.spot import Spot
from raspberry_pi.src.Kinematics.SpotKinematics import SpotModel
from raspberry_pi.src.IMU.IMU import IMU

# from GymEnvs.spot_bezier_env import spotBezierEnv
from simulator import Simulator



if __name__ == '__main__':
    kinematics = SpotModel()
    imu = IMU()
    # env = spotBezierEnv(
    #     render=True,
    #     on_rack=False,
    #     height_field=False,
    #     draw_foot_path=False,
    # )

    spot = Spot(kinematics, imu)
    
    simulation = Simulator(spot=spot)
    simulation.run(0.01)