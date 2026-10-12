"""The robot's position and heading on the floor from an overhead camera, independent of its own estimate:
uv run python ground_truth.py course.json --camera 0 --out track.csv   (or --video run.mp4)

An ArUco marker on the robot's back is found in each frame, and four ArUco markers lying on the floor at known
positions give the homography from the image to the floor (docs/benchmark.md lays the course out). The robot's marker
lies above the floor, so its corners, mapped onto the floor plane, sit too far from the point under the camera by
the ratio of the camera's height to its height over the marker; that is undone about the camera's nadir before the
marker's centre, its heading and the body's centre (through the marker's offset on the body) are taken. With a camera
calibration (--calibration, OpenCV's camera matrix and distortion coefficients as JSON) the corners are undistorted
first.

Each frame where the robot's marker is seen becomes a row of the track file: t (s), x and y (m, floor frame), heading
(rad, counter-clockwise from the floor's x axis). From a camera, t is time.monotonic() at the frame's arrival, the
clock record.py stamps its frames with; from a video, the video's own time. score_track.py aligns either with a
telemetry recording.

The course file (JSON):
    dictionary          the ArUco dictionary's name in OpenCV, e.g. "DICT_4X4_50"
    floor_markers       {id: [x, y]}: each floor marker's centre (m), its top edge towards +y (x right, y up, seen from
                        above)
    floor_marker_size   their black square's side (m)
    robot_marker        {"id", "height": its face above the floor (m), "offset": [forward, left] of its centre from
                        the body's centre (m), "yaw_deg": its top edge's angle counter-clockwise from the robot's nose}
    camera              {"nadir": [x, y] the floor point under the lens (m), "height": the lens above the floor (m)}
"""
import argparse
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

MIN_FLOOR_MARKERS = 3  # a homography from fewer, all on one side, extrapolates badly; the last good one stands
TRACK_COLUMNS = "t,x,y,heading"


@dataclass(frozen=True)
class Course:
    dictionary: str
    floor_markers: dict[int, tuple[float, float]]
    floor_marker_size: float
    robot_id: int
    robot_height: float
    robot_offset: tuple[float, float]
    robot_yaw_deg: float
    camera_nadir: tuple[float, float]
    camera_height: float

    @classmethod
    def load(cls, path) -> "Course":
        data = json.loads(Path(path).read_text())
        robot, camera = data["robot_marker"], data["camera"]
        return cls(data["dictionary"], {int(k): tuple(v) for k, v in data["floor_markers"].items()},
                   data["floor_marker_size"], robot["id"], robot["height"], tuple(robot["offset"]),
                   robot.get("yaw_deg", 0.0), tuple(camera["nadir"]), camera["height"])

    def aruco_dictionary(self):
        return cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, self.dictionary))


def marker_corners(centre, size: float) -> np.ndarray:
    """A floor marker's corners in ArUco's order (top left, top right, bottom right, bottom left), its top towards +y."""
    half = size / 2
    return np.asarray(centre, dtype=float) + half * np.array([[-1, 1], [1, 1], [1, -1], [-1, -1]], dtype=float)


@dataclass(frozen=True)
class Pose:
    x: float
    y: float
    heading: float  # rad


class Tracker:
    def __init__(self, course: Course, camera_matrix=None, dist_coeffs=None):
        self.course = course
        parameters = cv2.aruco.DetectorParameters()
        parameters.cornerRefinementMethod = cv2.aruco.CORNER_REFINE_SUBPIX
        self._detector = cv2.aruco.ArucoDetector(course.aruco_dictionary(), parameters)
        self._camera = None if camera_matrix is None else (np.asarray(camera_matrix, float), np.asarray(dist_coeffs, float))
        self.homography = None  # image (undistorted pixels) to floor (m)

    def _undistorted(self, corners: np.ndarray) -> np.ndarray:
        if self._camera is None:
            return corners
        matrix, dist = self._camera
        return cv2.undistortPoints(corners.reshape(-1, 1, 2), matrix, dist, P=matrix).reshape(-1, 2)

    def pose(self, image) -> Pose | None:
        """The body's pose in this frame, or None where its marker is not seen (or no homography is known yet)."""
        gray = image if image.ndim == 2 else cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        corners, ids, _ = self._detector.detectMarkers(gray)
        seen = {} if ids is None else {int(i): self._undistorted(c.reshape(4, 2)) for i, c in zip(ids.ravel(), corners)}
        floor = [i for i in self.course.floor_markers if i in seen]
        if len(floor) >= MIN_FLOOR_MARKERS:
            image_points = np.vstack([seen[i] for i in floor])
            floor_points = np.vstack([marker_corners(self.course.floor_markers[i], self.course.floor_marker_size)
                                      for i in floor])
            self.homography, _ = cv2.findHomography(image_points, floor_points)
        if self.homography is None or self.course.robot_id not in seen:
            return None
        on_floor = cv2.perspectiveTransform(seen[self.course.robot_id].reshape(-1, 1, 2), self.homography).reshape(4, 2)
        # The ray through each corner meets the floor further out than the corner by height / (height - marker's).
        nadir = np.asarray(self.course.camera_nadir)
        corners = nadir + (on_floor - nadir) * (1 - self.course.robot_height / self.course.camera_height)
        top = (corners[0] + corners[1]) / 2 - (corners[3] + corners[2]) / 2
        heading = np.arctan2(top[1], top[0]) - np.radians(self.course.robot_yaw_deg)
        forward, left = self.course.robot_offset
        c, s = np.cos(heading), np.sin(heading)
        centre = corners.mean(axis=0) - np.array([c * forward - s * left, s * forward + c * left])
        return Pose(float(centre[0]), float(centre[1]), float(np.arctan2(s, c)))


def write_markers(course: Course, directory, pixels: int = 600) -> list[Path]:
    """Each of the course's markers as a PNG with a white quiet zone of one marker cell, ready to print to size."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    for marker_id in [course.robot_id, *course.floor_markers]:
        image = cv2.aruco.generateImageMarker(course.aruco_dictionary(), marker_id, pixels)
        border = pixels // (course.aruco_dictionary().markerSize + 2)
        path = directory / f"aruco_{course.dictionary}_{marker_id}.png"
        cv2.imwrite(str(path), cv2.copyMakeBorder(image, border, border, border, border, cv2.BORDER_CONSTANT, value=255))
        written.append(path)
    return written


def load_track(path) -> np.ndarray:
    """A track file's rows of t, x, y, heading, below its comment line and its header."""
    return np.loadtxt(path, delimiter=",", skiprows=2, ndmin=2)


def track(capture, tracker: Tracker, out, from_camera: bool, seconds: float | None) -> tuple[int, int]:
    """Writes a row per frame the robot is seen in, until the video ends, the time is up or Ctrl-C; returns the frames
    read and the rows written."""
    frames = rows = 0
    deadline = None if seconds is None else time.monotonic() + seconds
    try:
        while deadline is None or time.monotonic() < deadline:
            ok, image = capture.read()
            if not ok:
                break
            t = time.monotonic() if from_camera else capture.get(cv2.CAP_PROP_POS_MSEC) / 1000.0
            frames += 1
            pose = tracker.pose(image)
            if pose is not None:
                out.write(f"{t:.6f},{pose.x:.5f},{pose.y:.5f},{pose.heading:.5f}\n")
                out.flush()
                rows += 1
    except KeyboardInterrupt:
        pass
    return frames, rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("course", help="the course file (JSON)")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--camera", type=int, help="a camera's index")
    source.add_argument("--video", help="a video file")
    source.add_argument("--write-markers", metavar="DIR", help="write the course's markers as PNGs and exit")
    parser.add_argument("--calibration", help="OpenCV camera calibration (JSON: camera_matrix, dist_coeffs)")
    parser.add_argument("--out", default="track.csv")
    parser.add_argument("--seconds", type=float, help="stop a camera after this long (default: Ctrl-C)")
    args = parser.parse_args()
    course = Course.load(args.course)
    if args.write_markers:
        for path in write_markers(course, args.write_markers):
            print(path)
        return 0
    calibration = json.loads(Path(args.calibration).read_text()) if args.calibration else {}
    tracker = Tracker(course, calibration.get("camera_matrix"), calibration.get("dist_coeffs"))
    from_camera = args.camera is not None
    capture = cv2.VideoCapture(args.camera if from_camera else args.video)
    if not capture.isOpened():
        print(f"cannot open {'camera ' + str(args.camera) if from_camera else args.video}")
        return 2
    clock = "host-monotonic" if from_camera else "video"
    with open(args.out, "w", newline="") as out:
        out.write(f"# ground_truth.py; clock={clock}; course={Path(args.course).name}\n{TRACK_COLUMNS}\n")
        frames, rows = track(capture, tracker, out, from_camera, args.seconds)
    capture.release()
    print(f"{rows} of {frames} frames saw the robot; wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
