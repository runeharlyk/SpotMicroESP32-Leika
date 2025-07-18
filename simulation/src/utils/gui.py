import pybullet as p
import numpy as np

from src.robot.kinematics import BodyStateT
from src.robot.gait import GaitStateT, GaitType, default_stand_frac, default_offset


class GUI:
    def __init__(self, bot):
        self.robot = bot
        self.c_yaw = 10
        self.c_pitch = -17
        self.c_distance = 5

        self.x_slider = p.addUserDebugParameter("x", -50, 50, 0)
        self.y_slider = p.addUserDebugParameter("y", -50, 50, 0)
        self.z_slider = p.addUserDebugParameter("z", -50, 50, 0)
        self.yaw_slider = p.addUserDebugParameter(
            "yaw", -np.pi / 4, np.pi / 4, 0)
        self.pitch_slider = p.addUserDebugParameter(
            "pitch", -np.pi / 4, np.pi / 4, 0)
        self.roll_slider = p.addUserDebugParameter(
            "roll", -np.pi / 4, np.pi / 4, 0)

        self.pivot_x_slider = p.addUserDebugParameter("pivot x", -50, 50, 0)
        self.pivot_y_slider = p.addUserDebugParameter("pivot y", -50, 50, 0)
        self.pivot_z_slider = p.addUserDebugParameter("pivot z", -50, 50, 0)

        self.step_x_slider = p.addUserDebugParameter("Step x", -50, 50, 0)
        self.step_z_slider = p.addUserDebugParameter("Step z", -50, 50, 0)
        self.angle_slider = p.addUserDebugParameter(
            "Angle", -np.pi / 4, np.pi / 4, 0)
        self.step_height_slider = p.addUserDebugParameter(
            "Step height", 0, 50, 15)
        self.step_depth_slider = p.addUserDebugParameter(
            "Step depth", 0, 0.01, 0.002)
        self.speed_slider = p.addUserDebugParameter("Speed", 0, 2, 1)
        self.stand_frac_slider = p.addUserDebugParameter(
            "Stand frac", 0, 1, 0.5)

        self.gait_type_slider = p.addUserDebugParameter(
            "Gait Type", 0, len(GaitType) - 1, 0)

        # self.gait_type_slider = p.addUserDebugParameter("Gait Type", 0, len(GaitType) - 1, 0, paramType=p.GUI_ENUM,
        #                               enumNames=[g.value for g in GaitType])
        self.last_gait_type = GaitType.TROT_GATE

    def update_gait_state(self, gait_state: GaitStateT):
        gait_state["step_x"] = p.readUserDebugParameter(self.step_x_slider)
        gait_state["step_z"] = p.readUserDebugParameter(self.step_z_slider)
        gait_state["step_angle"] = p.readUserDebugParameter(self.angle_slider)
        gait_state["step_height"] = p.readUserDebugParameter(
            self.step_height_slider)
        gait_state["step_depth"] = p.readUserDebugParameter(
            self.step_depth_slider)
        gait_state["step_velocity"] = p.readUserDebugParameter(
            self.speed_slider)
        gait_state["stand_frac"] = p.readUserDebugParameter(
            self.stand_frac_slider)
        gait_state["offset"] = default_offset[self.last_gait_type]

    def update_body_state(self, body_state: BodyStateT):
        body_state["xm"] = p.readUserDebugParameter(self.x_slider)
        body_state["ym"] = p.readUserDebugParameter(self.y_slider)
        body_state["zm"] = p.readUserDebugParameter(self.z_slider)
        body_state["omega"] = p.readUserDebugParameter(self.roll_slider)
        body_state["phi"] = p.readUserDebugParameter(self.pitch_slider)
        body_state["psi"] = p.readUserDebugParameter(self.yaw_slider)
        body_state["px"] = p.readUserDebugParameter(self.pivot_x_slider)
        body_state["py"] = p.readUserDebugParameter(self.pivot_y_slider)
        body_state["pz"] = p.readUserDebugParameter(self.pivot_z_slider)

    def update(self):
        gait_type = GaitType(
            int(p.readUserDebugParameter(self.gait_type_slider)))
        if gait_type != self.last_gait_type:
            self.last_gait_type = gait_type
            p.removeUserDebugItem(self.stand_frac_slider)
            self.stand_frac_slider = p.addUserDebugParameter(
                "Stand frac", 0, 1, default_stand_frac[gait_type])

        quadruped_pos, _ = p.getBasePositionAndOrientation(self.robot)
        p.resetDebugVisualizerCamera(
            cameraDistance=self.c_distance,
            cameraYaw=self.c_yaw,
            cameraPitch=self.c_pitch,
            cameraTargetPosition=quadruped_pos,
        )

        keys = p.getKeyboardEvents()
        if keys.get(ord("j")):
            self.c_yaw += 0.1
        if keys.get(ord("k")):
            self.c_yaw -= 0.1
        if keys.get(ord("m")):
            self.c_pitch += 0.1
        if keys.get(ord("i")):
            self.c_pitch -= 0.1

        if keys.get(ord("q")) or keys.get(27):
            p.disconnect()
            exit()

        self.position = np.array(
            [
                p.readUserDebugParameter(self.x_slider),
                p.readUserDebugParameter(self.y_slider),
                p.readUserDebugParameter(self.z_slider),
            ]
        )

        self.orientation = np.array(
            [
                p.readUserDebugParameter(self.roll_slider),
                p.readUserDebugParameter(self.pitch_slider),
                p.readUserDebugParameter(self.yaw_slider),
            ]
        )

        return self.position, self.orientation
