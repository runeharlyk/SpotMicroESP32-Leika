"""The Robot API in the simulation: uv run python visionary_demo.py (pass an address to drive the real robot)."""
import sys

from src.leika import Robot


def main() -> None:
    target = sys.argv[1] if len(sys.argv) > 1 else "simulation"
    with Robot(target) as robot:
        robot.stand()
        robot.sleep(1.0)
        robot.move_forward(0.2)
        robot.rotate(90)
        robot.rotate(-90)
        robot.move_sideways(0.05)
        robot.move_forward(-0.2)
        state = robot.state()
        print(f"roll {state.roll:.1f}  pitch {state.pitch:.1f}  yaw {state.yaw:.1f} degrees")


if __name__ == "__main__":
    main()
