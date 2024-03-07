import copy


class GaitState:
    def __init__(self) -> None:
        self.step_length = 0.1
        self.yaw_rate = 0
        self.lateral_fraction = 0
        self.step_velocity = 0.001
        self.swing_period = 0.2
        self.clearance_height = 0.045
        self.penetration_depth = 0.003
        self.contacts = [False] * 4

        self.target_step_length = 0
        self.target_yaw_rate = 0
        self.target_lateral_fraction = 0

    def update_gait_state(self, dt):
        self.step_length = self.step_length * (1 - dt) + self.target_step_length * dt
        self.lateral_fraction = (
            self.lateral_fraction * (1 - dt) + self.target_lateral_fraction * dt
        )
        self.yaw_rate = self.yaw_rate * (1 - dt) + self.target_yaw_rate * dt


class MotionController:
    def __init__(
        self,
        # env: spotBezierEnv,
        # gui: GUI,
        # bodyState: BodyState,
        # gaitState: GaitState,
        spot_model,
        gait,
    ) -> None:
        self.gait = gait
        self.gait_state = GaitState()
        self.spot_model = spot_model

        self.dt = 0.01

    def update_gait_state(self, command):
        self.gait_state.step_length = abs(command["lx"]) / 255

    def run(self, model, command):
        self.update_gait_state(command)
        self.gait_state.contacts = [False] * 4
        self.body_state.worldFeetPositions = copy.deepcopy(self.spot.WorldToFoot)

        model["transformation"]["world_feet_position"] = self.gait.generate_trajectory(
            model, self.gait_state, self.dt
        )
