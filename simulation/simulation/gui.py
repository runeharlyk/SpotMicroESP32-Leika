import pybullet as pb
import numpy as np
import sys


class GUI:
    def __init__(self, quadruped):
        self.cameraYaw = 0
        self.cameraPitch = -7
        self.cameraDist = 0.66

        self.rollId = pb.addUserDebugParameter("roll", -np.pi / 4, np.pi / 4, 0.0)
        self.pitchId = pb.addUserDebugParameter("pitch", -np.pi / 4, np.pi / 4, 0.0)
        self.yawId = pb.addUserDebugParameter("yaw", -np.pi / 4, np.pi / 4, 0.0)
        self.xId = pb.addUserDebugParameter("x", -0.10, 0.10, 0.0)
        self.yId = pb.addUserDebugParameter("y", -0.10, 0.10, 0.0)
        self.zId = pb.addUserDebugParameter("z", -0.055, 0.17, 0.0)

        self.quadruped = quadruped

    def updateCamera(self):
        quadruped_position, _ = pb.getBasePositionAndOrientation(self.quadruped)
        pb.resetDebugVisualizerCamera(
            cameraDistance=self.cameraDist,
            cameraYaw=self.cameraYaw,
            cameraPitch=self.cameraPitch,
            cameraTargetPosition=quadruped_position,
        )

    def handleInput(self):
        keys = pb.getKeyboardEvents()
        # Keys to change camera
        if keys.get(100):  # D
            self.cameraYaw += 1
        if keys.get(97):  # A
            self.cameraYaw -= 1
        if keys.get(99):  # C
            self.cameraPitch += 1
        if keys.get(102):  # F
            self.cameraPitch -= 1
        if keys.get(122):  # Z
            self.cameraDist += 0.01
        if keys.get(120):  # X
            self.cameraDist -= 0.01
        if keys.get(27):  # ESC
            pb.disconnect()
            sys.exit()

    def update(self):
        self.updateCamera()
        self.handleInput()

        position = np.array(
            [
                pb.readUserDebugParameter(self.xId),
                pb.readUserDebugParameter(self.yId),
                pb.readUserDebugParameter(self.zId),
            ]
        )
        orientation = np.array(
            [
                pb.readUserDebugParameter(self.rollId),
                pb.readUserDebugParameter(self.pitchId),
                pb.readUserDebugParameter(self.yawId),
            ]
        )

        return (
            position,
            orientation,
        )
