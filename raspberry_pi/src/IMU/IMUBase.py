class IMUBase():
    def __init__(self) -> None:
        pass

    def read_orientation(self):
        raise NotImplementedError()