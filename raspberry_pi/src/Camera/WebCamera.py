import cv2
from .CameraBase import CameraBase


class WebCamera(CameraBase):
    def __init__(self, camera_id=0):
        self.cap = cv2.VideoCapture(camera_id)
        # super().__init__()

    def get_image(self):
        ret, frame = self.cap.read()
        if ret:
            return frame
        else:
            return None  # Return None if the frame could not be captured

    def __del__(self):
        self.cap.release()