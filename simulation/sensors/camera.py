import cv2
import numpy as np
import pybullet as pb
import sys

sys.path.append("../../../")

from raspberry_pi.src.Camera.CameraBase import CameraBase

viewMatrix = pb.computeViewMatrix(
    cameraEyePosition=[0, 0, 1], 
    cameraTargetPosition=[0, 0, 0], 
    cameraUpVector=[0, 1, 0])
projectionMatrix = pb.computeProjectionMatrixFOV(
    fov=160, aspect=1.0, nearVal=0.1, farVal=150.0)

class PyBulletCamera(CameraBase):
    def __init__(self, view_matrix=viewMatrix, projection_matrix=projectionMatrix, width=640, height=480):
        """
        Initializes the camera with PyBullet view and projection matrices.
        :param view_matrix: The view matrix for the camera in PyBullet.
        :param projection_matrix: The projection matrix for the camera.
        :param width: Width of the camera image.
        :param height: Height of the camera image.
        """
        self.view_matrix = view_matrix
        self.projection_matrix = projection_matrix
        self.width = width
        self.height = height

    def get_image(self):
        """
        Captures an image from the PyBullet simulation.
        :return: An image frame captured from PyBullet.
        """
        # Capture an image from the simulation
        _, _, px, _, _ = pb.getCameraImage(
            width=self.width, height=self.height,
            viewMatrix=self.view_matrix,
            projectionMatrix=self.projection_matrix)
        
        # Convert PyBullet's RGBA image to an OpenCV BGR image
        rgb_image = np.array(px, dtype=np.uint8)
        rgb_image = np.reshape(rgb_image, (self.height, self.width, -1))
        bgr_image = cv2.cvtColor(rgb_image, cv2.COLOR_RGBA2BGR)
        
        return bgr_image
    
    def update_view_matrix(self, model_id):
        position, orientation = pb.getBasePositionAndOrientation(model_id)
        position = [position[0], position[1]-0.1, position[2]]
        orientation = [orientation[0], orientation[1], orientation[2]]

        # Calculate the camera's view matrix based on the new position and orientation
        camera_eye_pos = position  # This should be adjusted based on your specific relative position requirements
        camera_target_pos = [position[0] + orientation[0], position[1] + orientation[1], position[2] + orientation[2]]
        self.view_matrix = pb.computeViewMatrix(camera_eye_pos, camera_target_pos, [0, 1, 0])
