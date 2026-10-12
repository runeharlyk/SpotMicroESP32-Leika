# Floor benchmark

The robot's odometry and goal moves are only as good as the floor says they are.
The benchmark measures them against an overhead camera that tracks a fiducial on the robot's back, independent of anything the robot estimates.
Three scripts in `simulation/` do the work: `ground_truth.py` turns the camera's frames into a track of the body's position and heading on the floor, `record.py` records the robot's telemetry, and `score_track.py` aligns the two and scores the odometry and each goal move.

## The course

The course is a rectangle of floor, 2.0 m by 1.5 m, with an ArUco marker lying flat at each corner and a fifth on the robot.
The floor frame has its origin at the centre of marker 1, x along the long side and y along the short side, z up.

| Marker | Where (m)  | Size          | What it is                                   |
| ------ | ---------- | ------------- | -------------------------------------------- |
| 0      | on the robot | 8 cm square | the robot's marker, on a flat white plate on its back |
| 1      | (0.0, 0.0) | 15 cm square  | floor, origin                                |
| 2      | (2.0, 0.0) | 15 cm square  | floor                                        |
| 3      | (2.0, 1.5) | 15 cm square  | floor                                        |
| 4      | (0.0, 1.5) | 15 cm square  | floor                                        |

The markers come from OpenCV's `DICT_4X4_50`; `ground_truth.py course.json --write-markers markers/` writes them as PNGs, each with a white quiet zone, to print so that the black square has the size in the table.
Each floor marker lies with its top edge towards +y, and its position is its centre.
Measure the positions with a tape after laying them, and check that the two diagonals agree; whatever you measure goes into the course file, not the nominal values.

The camera looks straight down from about 2.4 m above the course's centre, (1.0, 0.75).
A webcam with a 70 degree horizontal field of view sees about 3.4 m by 1.9 m from there, which takes in all four floor markers with a margin.
At 1920 by 1080 a pixel is then about 1.8 mm on the floor and the robot's marker spans about 45 pixels.
Hang a plumb line from the lens to find the floor point under it (the nadir) and measure the lens's height above the floor; both go into the course file.

The robot's marker lies above the floor, so the homography that the floor markers give, which maps the floor plane, puts it too far from the nadir by the ratio of the camera's height to the camera's height over the marker.
`ground_truth.py` undoes that about the nadir, which is why it needs the marker's height, the camera's height and the nadir.
The correction is forgiving of the measurements: with the marker 0.12 m up, a camera height off by 5 cm moves a point 1 m from the nadir by 1 mm, and a nadir off by 2 cm moves every point by 1 mm.
The marker rides up and down with the body, though: 1 cm of height moves a point 1 m from the nadir by 4 mm, so measure the marker's height in the stand the course walks at.

The course file, `course.json`, for the layout above:

```json
{
  "dictionary": "DICT_4X4_50",
  "floor_markers": {"1": [0.0, 0.0], "2": [2.0, 0.0], "3": [2.0, 1.5], "4": [0.0, 1.5]},
  "floor_marker_size": 0.15,
  "robot_marker": {"id": 0, "height": 0.12, "offset": [0.0, 0.0], "yaw_deg": 0.0},
  "camera": {"nadir": [1.0, 0.75], "height": 2.4}
}
```

`offset` is where the robot's marker sits from the body's centre (forward and left, in metres), and `yaw_deg` the angle of the marker's top edge counter-clockwise from the robot's nose; with the marker centred and its top edge towards the nose both are zero.
A wide or cheap lens bends straight lines; calibrate it with OpenCV's chessboard calibration and pass the camera matrix and distortion coefficients as JSON (`camera_matrix`, `dist_coeffs`) with `--calibration`.

## Accuracy

On rendered frames of this kind (`simulation/tests/test_ground_truth.py`: a pinhole camera 1.8 m up, tilted, with an 8 cm marker 0.12 m above the floor riding a known path, written to an MJPG video), the track comes within 1 mm RMS (under 2 mm at worst) of the body's path and within 0.43 degrees RMS (under 1 degree at worst) of its heading.
Without the height correction the same frames are off by up to 4.7 cm.
With a lens that bends the image (radial distortion -0.25), the calibration brings the track back within the same tolerances.
A real camera adds motion blur, rolling shutter and lighting, and the floor markers' measured positions carry the tape's error, so treat these figures as the floor of what to expect.
Before a run, lay the robot's plate flat on the floor at a few taped points (with `height` set to 0 for that check) and compare the track with them.

## Running the benchmark

Run everything on one computer, so that the camera's frames and the telemetry carry the same monotonic clock; `score_track.py` finds the remaining offset either way, and from a video it has to.

1. Stand the robot at the course's start, inside the floor markers, and reset its odometry.
1. Start the track and the recording:

   ```bash
   uv run python ground_truth.py course.json --camera 0 --out run.csv
   uv run python record.py --robot 192.168.1.39 --out run.leika
   ```

1. Drive the course with the Python client. Open with the start motion, a quarter turn in place from standing, which gives the clock search a sharp feature, and stand at least 1.5 s between goals, since each goal's end points are the robot's mean pose over the half second before it starts and from half a second after it ends:

   ```python
   from src.leika import Robot

   with Robot("192.168.1.39") as robot:
       robot.stand()
       robot.reset_odometry()
       robot.sleep(2.0)
       for forward, sideways, turn in [(0, 0, 90), (0, 0, -90), (1.0, 0, 0), (0, 0, 90), (0, 0.3, 0), (0, 0, 180)]:
           robot.goal_move(forward=forward, sideways=sideways, turn_deg=turn)
           robot.sleep(1.5)
   ```

1. Stop both with Ctrl-C and score the run:

   ```bash
   uv run python score_track.py run.leika run.csv
   ```

`score_track.py` reports the clock offset it found and how well the start motion correlated (near 1 when the search found it), then for each stretch of odometry between resets the position error's RMS and end value, the heading error's RMS, end value and drift per metre walked, and the path the odometry counted against the path the camera saw, whose ratio is the odometry's distance scale.
For each goal move it reports where the robot ended against the goal's target, in the frame it faced when the goal started, and where its odometry thought it ended against where it did.
`record.py` keeps the goal statuses for this; a recording from before it did scores the odometry but no goals.
