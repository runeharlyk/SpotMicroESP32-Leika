import math
from queue import Empty
import cv2
import numpy as np
import pybullet as pb
import sys

sys.path.append("../../../")

from raspberry_pi.src.camera.CameraBase import CameraBase

viewMatrix = pb.computeViewMatrix(
    cameraEyePosition=[0, 0, 1], 
    cameraTargetPosition=[0, 0, 0], 
    cameraUpVector=[0, 1, 0])
projectionMatrix = pb.computeProjectionMatrixFOV(
    fov=160, aspect=1.0, nearVal=0.1, farVal=150.0)

distance = 100000

class PyBulletCamera(CameraBase):
    def __init__(self, model_id, frame_queue, view_matrix=viewMatrix, projection_matrix=projectionMatrix, width=640, height=480):
        """
        Initializes the camera with PyBullet view and projection matrices.
        :param view_matrix: The view matrix for the camera in PyBullet.
        :param projection_matrix: The projection matrix for the camera.
        :param width: Width of the camera image.
        :param height: Height of the camera image.
        """
        super().__init__()
        self.model_id = model_id
        self.view_matrix = view_matrix
        self.projection_matrix = projection_matrix
        self.width = width
        self.height = height
        self.frame_queue = frame_queue

    def update(self):
        """
        Captures an image from the PyBullet simulation.
        :return: An image frame captured from PyBullet.
        """

        self.update_view_matrix()
        frame = self.get_frame()
        if frame is not None:
            # Only keep the latest frame to avoid filling up the queue
            while not self.frame_queue.empty():
                try:
                    self.frame_queue.get_nowait()
                except Empty:
                    pass
            self.frame_queue.put(frame)
    
    def get_frame(self):
        # Capture an image from the simulation
        imgs = pb.getCameraImage(
            width=self.width, height=self.height,
            viewMatrix=self.view_matrix,
            projectionMatrix=self.projection_matrix, 
            shadow=True, renderer=pb.ER_BULLET_HARDWARE_OPENGL)
        _, _, px, _, _ = imgs
        
        # Convert PyBullet's RGBA image to an OpenCV BGR image
        rgb_image = np.array(px, dtype=np.uint8)
        rgb_image = np.reshape(rgb_image, (self.height, self.width, -1))
        return cv2.cvtColor(rgb_image, cv2.COLOR_RGBA2BGR)


    def update_view_matrix(self):

        position, orientation = pb.getBasePositionAndOrientation(self.model_id)
        yaw = pb.getEulerFromQuaternion(orientation)[-1]
        xA, yA, zA = position
        zA = zA + 0.3 # make the camera a little higher than the robot

        # compute focusing point of the camera
        xB = xA + math.cos(yaw) * distance
        yB = yA + math.sin(yaw) * distance
        zB = zA

        self.view_matrix = pb.computeViewMatrix(
            cameraEyePosition=[xA, yA, zA],
            cameraTargetPosition=[xB, yB, zB],
            cameraUpVector=[0, 0, 1.0]
        )

        self.projection_matrix = pb.computeProjectionMatrixFOV(fov=90, aspect=1.5, nearVal=0.02, farVal=3.5)

        # imgs = pb.getCameraImage(self.width, self.height,
        #                         view_matrix,
        #                         self.projection_matrix, shadow=True,
        #                         renderer=pb.ER_BULLET_HARDWARE_OPENGL)
        # position = [position[0], position[1], position[2]]
        # orientation = [orientation[0], orientation[1], orientation[2]]

        # # Calculate the camera's view matrix based on the new position and orientation
        # camera_eye_pos = position  # This should be adjusted based on your specific relative position requirements
        # camera_target_pos = [position[0] + orientation[0], position[1] + orientation[1], position[2] + orientation[2]]
        # self.view_matrix = pb.computeViewMatrix(camera_eye_pos, camera_target_pos, [0, 1, 0])
