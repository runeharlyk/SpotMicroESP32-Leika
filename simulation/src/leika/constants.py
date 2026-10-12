from enum import Enum, auto

class Gait(Enum):
    TROT = 0
    CRAWL = 1
    AUTO = 2  # the robot picks the crawl or the trot from the sticks, so a velocity has no fixed sticks under it

class Mode(Enum):
    DEACTIVATED = 0
    IDLE = 1
    CALIBRATION = 2
    REST = 3
    STAND = 4
    WALK = 5
