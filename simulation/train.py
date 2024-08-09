from simulation.environment import QuadrupedEnv
from training.trainer import Trainer

import resources as resources


def main():
    env = QuadrupedEnv(resources.getDataPath() + "/spot.urdf")
    trainer = Trainer(env)
    trainer.train()


if __name__ == "__main__":
    main()
