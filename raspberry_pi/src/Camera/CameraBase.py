from abc import abstractmethod


class CameraBase():
    def __init__(self) -> None:
        pass

    @abstractmethod
    def update(self) -> None:
        raise NotImplementedError