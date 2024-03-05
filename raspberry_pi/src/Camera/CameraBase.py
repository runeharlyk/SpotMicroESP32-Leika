from abc import abstractmethod
from typing import Any


class CameraBase():
    def __init__(self) -> None:
        pass

    @abstractmethod
    def get_image(self) -> Any:
        raise NotImplementedError