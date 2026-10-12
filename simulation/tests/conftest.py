import os
import shutil

import pytest

import virtual_robot


@pytest.fixture(scope="session")
def binary(tmp_path_factory):
    """The virtual robot, built once for the session."""
    if shutil.which(os.environ.get("CXX", "g++")) is None:
        pytest.skip("no host compiler: set CXX to a C++20 g++")
    return virtual_robot.build(str(tmp_path_factory.mktemp("virtual_robot") / "virtual_robot.exe"))
