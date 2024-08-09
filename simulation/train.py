from simulation.environment import QuadrupedEnv
from training.trainer import Trainer

import resources as resources

render = True

def main():
    env = QuadrupedEnv(resources.getDataPath() + "/spot.urdf")
    trainer = Trainer(env, render)
    trainer.train()


if __name__ == "__main__":
    main()
