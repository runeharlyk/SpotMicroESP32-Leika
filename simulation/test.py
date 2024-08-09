from simulation.environment import QuadrupedEnv
from training.model import SimpleNN

import resources as resources


def main():
    env = QuadrupedEnv(resources.getDataPath() + "/spot.urdf")
    env.reset()

    input_size = env.robot.get_observation().shape[0]
    output_size = env.robot.get_observation().shape[0]
    agent = SimpleNN(input_size, output_size)

    done = False
    observation = []

    while not done:
        action = agent.select_action(observation)
        observation, reward, done = env.step(action)


if __name__ == "__main__":
    main()
