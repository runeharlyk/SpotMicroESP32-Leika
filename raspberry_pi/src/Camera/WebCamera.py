import cv2
from .CameraBase import CameraBase


class WebCamera(CameraBase):
    def __init__(self, camera_id=0):
        self.cap = cv2.VideoCapture(camera_id)
        self._last_frame = None
        super().__init__()

    def get_image(self):
        self._last_frame

    def update(self) -> None:
        ret, frame = self.cap.read()
        if ret:
            self._last_frame = frame.copy()

    def __del__(self):
        self.cap.release()