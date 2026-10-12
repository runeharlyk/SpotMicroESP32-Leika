"""ground_truth.py on rendered frames: a pinhole camera looking down at the floor markers and at a marker riding a known
path at the robot's height, written to a video and tracked back."""
import json
import subprocess
import sys

import cv2
import numpy as np
import pytest

import ground_truth
from ground_truth import Course, Tracker

WIDTH, HEIGHT, FOCAL = 1280, 720, 900.0
CAMERA = np.array([0.8, 0.15, 1.8])  # floor frame, z up
LOOK_AT = np.array([0.8, 0.55, 0.0])
FLOOR_PX_PER_M = 800
FLOOR_EXTENT = ((-0.3, 1.9), (-0.3, 1.3))  # x and y of the floor texture (m)
ROBOT_MARKER_SIZE = 0.08
FPS = 30.0
SECONDS = 3.0
COURSE = {
    "dictionary": "DICT_4X4_50",
    "floor_markers": {"1": [0.0, 0.0], "2": [1.6, 0.0], "3": [1.6, 1.0], "4": [0.0, 1.0]},
    "floor_marker_size": 0.15,
    "robot_marker": {"id": 0, "height": 0.12, "offset": [0.03, -0.01], "yaw_deg": 90.0},
    "camera": {"nadir": CAMERA[:2].tolist(), "height": float(CAMERA[2])},
}
# The tolerances the track meets on these frames, where a pixel is about 2 mm on the floor: it comes within 1 mm RMS
# and 0.43 degrees RMS (an 8 cm marker spans about 38 pixels).
MAX_POSITION_RMS_M, MAX_POSITION_ERROR_M = 0.002, 0.005
MAX_HEADING_RMS_DEG, MAX_HEADING_ERROR_DEG = 0.6, 1.5


def camera_matrix():
    return np.array([[FOCAL, 0, WIDTH / 2], [0, FOCAL, HEIGHT / 2], [0, 0, 1]])


def projection():
    """K [R | -R C] for a camera at CAMERA looking at LOOK_AT, image x to the right."""
    forward = (LOOK_AT - CAMERA) / np.linalg.norm(LOOK_AT - CAMERA)
    right = np.cross(forward, [0.0, 0.0, 1.0])
    right /= np.linalg.norm(right)
    down = np.cross(forward, right)
    rotation = np.vstack([right, down, forward])
    return camera_matrix() @ np.column_stack([rotation, -rotation @ CAMERA])


def plane_homography(z: float) -> np.ndarray:
    """Floor-frame (x, y) on the plane at height z to pixels."""
    p = projection()
    return np.column_stack([p[:, 0], p[:, 1], p[:, 2] * z + p[:, 3]])


def floor_texture(course: Course) -> tuple[np.ndarray, np.ndarray]:
    (x0, x1), (y0, y1) = FLOOR_EXTENT
    texture = np.full((round((y1 - y0) * FLOOR_PX_PER_M), round((x1 - x0) * FLOOR_PX_PER_M)), 150, np.uint8)
    side = round(course.floor_marker_size * FLOOR_PX_PER_M)
    for marker_id, (x, y) in course.floor_markers.items():
        marker = cv2.aruco.generateImageMarker(course.aruco_dictionary(), marker_id, side)
        u, v = round((x - x0) * FLOOR_PX_PER_M - side / 2), round((y1 - y) * FLOOR_PX_PER_M - side / 2)
        texture[v - side // 4 : v + side + side // 4, u - side // 4 : u + side + side // 4] = 255
        texture[v : v + side, u : u + side] = marker
    to_floor = np.array([[1 / FLOOR_PX_PER_M, 0, x0], [0, -1 / FLOOR_PX_PER_M, y1], [0, 0, 1]])
    return texture, plane_homography(0.0) @ to_floor


def marker_layer(course: Course, centre, top_angle: float):
    """The robot's marker, on a white plate twice its size, as pixels and the homography that puts it in the image."""
    side = 240
    marker = cv2.aruco.generateImageMarker(course.aruco_dictionary(), course.robot_id, side)
    plate = cv2.copyMakeBorder(marker, side // 2, side // 2, side // 2, side // 2, cv2.BORDER_CONSTANT, value=255)
    metres = ROBOT_MARKER_SIZE / side
    rotate = top_angle - np.pi / 2  # turns the plate's up, +y, to top_angle
    c, s = np.cos(rotate), np.sin(rotate)
    half = plate.shape[0] / 2
    to_plane = np.array([[c, s, centre[0]], [s, -c, centre[1]], [0, 0, 1]]) @ np.array(
        [[metres, 0, -half * metres], [0, metres, -half * metres], [0, 0, 1]])
    return plate, plane_homography(course.robot_height) @ to_plane


def body_path(t):
    """The body's centre and heading along an S through the course, its heading along its velocity."""
    x = 0.8 + 0.45 * np.sin(0.9 * t)
    y = 0.5 + 0.2 * np.sin(1.8 * t)
    heading = np.arctan2(0.36 * np.cos(1.8 * t), 0.405 * np.cos(0.9 * t))
    return x, y, heading


def render(course: Course, floor, t: float) -> np.ndarray:
    texture, to_image = floor
    frame = cv2.warpPerspective(texture, to_image, (WIDTH, HEIGHT), borderValue=90)
    x, y, heading = body_path(t)
    forward, left = course.robot_offset
    centre = np.array([x + np.cos(heading) * forward - np.sin(heading) * left,
                       y + np.sin(heading) * forward + np.cos(heading) * left])
    plate, plate_to_image = marker_layer(course, centre, heading + np.radians(course.robot_yaw_deg))
    layer = cv2.warpPerspective(plate, plate_to_image, (WIDTH, HEIGHT))
    mask = cv2.warpPerspective(np.full_like(plate, 255), plate_to_image, (WIDTH, HEIGHT)) > 127
    frame[mask] = layer[mask]
    return cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)


def write_video(path, course: Course, distort=None) -> None:
    writer = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"MJPG"), FPS, (WIDTH, HEIGHT))
    floor = floor_texture(course)
    for k in range(round(SECONDS * FPS)):
        frame = render(course, floor, k / FPS)
        writer.write(frame if distort is None else distort(frame))
    writer.release()


def errors(track: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    x, y, heading = body_path(track[:, 0])
    position = np.hypot(track[:, 1] - x, track[:, 2] - y)
    turn = np.degrees(np.angle(np.exp(1j * (track[:, 3] - heading))))
    return position, turn


@pytest.fixture(scope="module")
def course_file(tmp_path_factory):
    path = tmp_path_factory.mktemp("course") / "course.json"
    path.write_text(json.dumps(COURSE))
    return path


@pytest.fixture(scope="module")
def video(tmp_path_factory, course_file):
    path = tmp_path_factory.mktemp("video") / "run.avi"
    write_video(path, Course.load(course_file))
    return path


def test_the_track_follows_the_markers_path_within_its_tolerances(tmp_path, course_file, video):
    out = tmp_path / "track.csv"
    subprocess.run([sys.executable, "ground_truth.py", str(course_file), "--video", str(video), "--out", str(out)],
                   check=True, capture_output=True)
    assert "clock=video" in out.read_text().splitlines()[0]
    track = ground_truth.load_track(out)
    assert len(track) == round(SECONDS * FPS)
    np.testing.assert_allclose(track[:, 0], np.arange(len(track)) / FPS, atol=1e-3)
    position, turn = errors(track)
    assert np.sqrt(np.mean(position ** 2)) < MAX_POSITION_RMS_M and position.max() < MAX_POSITION_ERROR_M
    assert np.sqrt(np.mean(turn ** 2)) < MAX_HEADING_RMS_DEG and np.abs(turn).max() < MAX_HEADING_ERROR_DEG


def test_without_the_markers_height_the_track_falls_out_of_tolerance(course_file, video):
    course = Course.load(course_file)
    flat = Tracker(Course(**{**course.__dict__, "robot_height": 0.0}))
    capture = cv2.VideoCapture(str(video))
    rows = []
    while (read := capture.read())[0]:
        pose = flat.pose(read[1])
        rows.append((capture.get(cv2.CAP_PROP_POS_MSEC) / 1000.0, pose.x, pose.y, pose.heading))
    position, _ = errors(np.array(rows))
    assert position.max() > 4 * MAX_POSITION_ERROR_M  # the parallax ground_truth.py corrects is real


def test_a_distorting_lens_is_undone_by_the_cameras_calibration(tmp_path, course_file):
    course = Course.load(course_file)
    dist = np.array([-0.25, 0.08, 0.0, 0.0, 0.0])
    # Each pixel of the distorted frame shows the ideal frame's point its ray comes from.
    grid = np.stack(np.meshgrid(np.arange(WIDTH, dtype=np.float32), np.arange(HEIGHT, dtype=np.float32)), -1)
    ideal = cv2.undistortPoints(grid.reshape(-1, 1, 2), camera_matrix(), dist, P=camera_matrix()).reshape(HEIGHT, WIDTH, 2)
    distort = lambda frame: cv2.remap(frame, ideal[..., 0], ideal[..., 1], cv2.INTER_LINEAR, borderValue=(90, 90, 90))
    video = tmp_path / "distorted.avi"
    write_video(video, course, distort)

    def track_with(tracker: Tracker) -> np.ndarray:
        capture, rows = cv2.VideoCapture(str(video)), []
        while (read := capture.read())[0]:
            pose = tracker.pose(read[1])
            if pose is not None:
                rows.append((capture.get(cv2.CAP_PROP_POS_MSEC) / 1000.0, pose.x, pose.y, pose.heading))
        return np.array(rows)

    position, turn = errors(track_with(Tracker(course, camera_matrix(), dist)))
    assert len(position) == round(SECONDS * FPS)
    assert np.sqrt(np.mean(position ** 2)) < MAX_POSITION_RMS_M and position.max() < MAX_POSITION_ERROR_M
    assert np.abs(turn).max() < MAX_HEADING_ERROR_DEG
    uncorrected, _ = errors(track_with(Tracker(course)))
    assert uncorrected.max() > 2 * MAX_POSITION_ERROR_M


def test_the_printed_markers_read_back_as_the_courses_ids(tmp_path, course_file):
    course = Course.load(course_file)
    detector = cv2.aruco.ArucoDetector(course.aruco_dictionary(), cv2.aruco.DetectorParameters())
    for path in ground_truth.write_markers(course, tmp_path):
        _, ids, _ = detector.detectMarkers(cv2.imread(str(path), cv2.IMREAD_GRAYSCALE))
        assert ids is not None and path.stem.endswith(f"_{int(ids[0][0])}")
    assert len(list(tmp_path.glob("*.png"))) == 1 + len(course.floor_markers)
