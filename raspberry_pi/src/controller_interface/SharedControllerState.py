import threading

from src.controller_interface.ControllerState import ControllerState 

class SharedState:
    def __init__(self):
        self.lock = threading.Lock()
        self.state = ControllerState()

    def update_state(self, new_state):
        with self.lock:
            self.state = new_state

    def get_latest_state(self):
        with self.lock:
            return self.state

shared_state = SharedState()