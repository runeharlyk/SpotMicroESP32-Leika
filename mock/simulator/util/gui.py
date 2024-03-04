#!/usr/bin/env python

import pybullet as pb
import time
import numpy as np
import sys


class GUI:
    def __init__(self, quadruped):

        time.sleep(0.5)

        self.cyaw = 0
        self.cpitch = -7
        self.cdist = 0.66

        self.xId = pb.addUserDebugParameter("x", -0.10, 0.10, 0.)
        self.yId = pb.addUserDebugParameter("y", -0.10, 0.10, 0.)
        self.zId = pb.addUserDebugParameter("z", -0.055, 0.17, 0.)
        self.rollId = pb.addUserDebugParameter("roll", -np.pi / 4, np.pi / 4, 0.0)
        self.pitchId = pb.addUserDebugParameter("pitch", -np.pi / 4, np.pi / 4, 0.0)
        self.yawId = pb.addUserDebugParameter("yaw", -np.pi / 4, np.pi / 4, 0.)
        self.StepLengthID = pb.addUserDebugParameter("Step Length", -0.1, 0.1, 0.1)
        self.YawRateId = pb.addUserDebugParameter("Yaw Rate", -2.0, 2.0, 0.)
        self.LateralFractionId = pb.addUserDebugParameter(
            "Lateral Fraction", -np.pi / 2.0, np.pi / 2.0, 0.0
        )
        self.StepVelocityId = pb.addUserDebugParameter(
            "Step Velocity", 0.001, 3.0, 0.001
        )
        self.SwingPeriodId = pb.addUserDebugParameter("Swing Period", 0.1, 0.4, 0.2)

        self.ClearanceHeightId = pb.addUserDebugParameter(
            "Clearance Height", 0.0, 0.1, 0.045
        )
        self.PenetrationDepthId = pb.addUserDebugParameter(
            "Penetration Depth", 0.0, 0.05, 0.003
        )

        self.quadruped = quadruped

    def UserInput(self, bodyState, gaitState):

        quadruped_pos, _ = pb.getBasePositionAndOrientation(self.quadruped)
        pb.resetDebugVisualizerCamera(cameraDistance=self.cdist,
                                      cameraYaw=self.cyaw,
                                      cameraPitch=self.cpitch,
                                      cameraTargetPosition=quadruped_pos)
        keys = pb.getKeyboardEvents()
        # Keys to change camera
        if keys.get(100):  # D
            self.cyaw += 1
        if keys.get(97):  # A
            self.cyaw -= 1
        if keys.get(99):  # C
            self.cpitch += 1
        if keys.get(102):  # F
            self.cpitch -= 1
        if keys.get(122):  # Z
            self.cdist += .01
        if keys.get(120):  # X
            self.cdist -= .01
        if keys.get(27):  # ESC
            pb.disconnect()
            sys.exit()

        # Read Robot Transform from GUI
        bodyState.position = np.array(
            [
                pb.readUserDebugParameter(self.xId),
                pb.readUserDebugParameter(self.yId),
                pb.readUserDebugParameter(self.zId),
            ]
        )
        bodyState.rotation = np.array(
            [
                pb.readUserDebugParameter(self.rollId),
                pb.readUserDebugParameter(self.pitchId),
                pb.readUserDebugParameter(self.yawId),
            ]
        )
        gaitState.target_step_length = pb.readUserDebugParameter(self.StepLengthID)
        gaitState.target_yaw_rate = pb.readUserDebugParameter(self.YawRateId)
        gaitState.target_lateral_fraction = pb.readUserDebugParameter(
            self.LateralFractionId
        )
        gaitState.step_velocity = pb.readUserDebugParameter(self.StepVelocityId)
        gaitState.clearance_height = pb.readUserDebugParameter(self.ClearanceHeightId)
        gaitState.penetration_depth = pb.readUserDebugParameter(self.PenetrationDepthId)
        gaitState.swing_period = pb.readUserDebugParameter(self.SwingPeriodId)
