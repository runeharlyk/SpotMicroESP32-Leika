import argparse
import os
import gymnasium as gym
from stable_baselines3 import PPO, SAC
from stable_baselines3.common.callbacks import CheckpointCallback, EvalCallback
from stable_baselines3.common.vec_env import DummyVecEnv, SubprocVecEnv, VecNormalize
from stable_baselines3.common.monitor import Monitor
import numpy as np
import torch

from src.envs.quadruped_env import QuadrupedEnv, TerrainType


def make_env(terrain_type=TerrainType.FLAT, render_mode="rgb_array"):
    def _init():
        env = QuadrupedEnv(terrain_type=terrain_type, render_mode=render_mode)
        env = Monitor(env)
        return env

    return _init


def train_ppo(
    total_timesteps=1_000_000,
    learning_rate=3e-4,
    n_steps=2048,
    batch_size=64,
    n_epochs=10,
    gamma=0.99,
    gae_lambda=0.95,
    clip_range=0.2,
    ent_coef=0.01,
    vf_coef=0.5,
    max_grad_norm=0.5,
    save_dir="models/ppo",
    log_dir="logs/ppo",
    eval_freq=10000,
    save_freq=50000,
    terrain_type=TerrainType.FLAT,
    n_envs=8,
    use_gpu=True,
):
    os.makedirs(save_dir, exist_ok=True)
    os.makedirs(log_dir, exist_ok=True)
    os.makedirs(f"{log_dir}/eval", exist_ok=True)

    print(f"Creating {n_envs} parallel training environments...")
    env = SubprocVecEnv([make_env(terrain_type=terrain_type) for _ in range(n_envs)])
    env = VecNormalize(env, norm_obs=True, norm_reward=True, clip_obs=10.0)

    print("Creating evaluation environment...")
    eval_env = DummyVecEnv([make_env(terrain_type=terrain_type)])
    eval_env = VecNormalize(eval_env, norm_obs=True, norm_reward=False, clip_obs=10.0)

    checkpoint_callback = CheckpointCallback(
        save_freq=save_freq,
        save_path=save_dir,
        name_prefix="ppo_checkpoint",
        save_vecnormalize=True,
    )

    eval_callback = EvalCallback(
        eval_env,
        best_model_save_path=f"{save_dir}/best",
        log_path=f"{log_dir}/eval",
        eval_freq=eval_freq,
        deterministic=True,
        render=False,
    )

    device = "cuda" if use_gpu and torch.cuda.is_available() else "cpu"
    print(f"Creating PPO model on device: {device}")
    model = PPO(
        "MlpPolicy",
        env,
        learning_rate=learning_rate,
        n_steps=n_steps,
        batch_size=batch_size,
        n_epochs=n_epochs,
        gamma=gamma,
        gae_lambda=gae_lambda,
        clip_range=clip_range,
        ent_coef=ent_coef,
        vf_coef=vf_coef,
        max_grad_norm=max_grad_norm,
        verbose=1,
        tensorboard_log=log_dir,
        device=device,
        policy_kwargs=dict(
            net_arch=[dict(pi=[256, 256], vf=[256, 256])],
            activation_fn=torch.nn.ReLU,
        ),
    )

    print(f"Starting PPO training for {total_timesteps} timesteps...")
    model.learn(
        total_timesteps=total_timesteps,
        callback=[checkpoint_callback, eval_callback],
        progress_bar=True,
    )

    print(f"Saving final model to {save_dir}/ppo_final")
    model.save(f"{save_dir}/ppo_final")
    env.save(f"{save_dir}/ppo_final_vecnormalize.pkl")

    print("Training complete!")
    return model, env


def train_sac(
    total_timesteps=1_000_000,
    learning_rate=3e-4,
    buffer_size=300000,
    learning_starts=10000,
    batch_size=256,
    tau=0.005,
    gamma=0.99,
    train_freq=1,
    gradient_steps=1,
    ent_coef="auto",
    save_dir="models/sac",
    log_dir="logs/sac",
    eval_freq=10000,
    save_freq=50000,
    terrain_type=TerrainType.FLAT,
    n_envs=8,
    use_gpu=True,
):
    os.makedirs(save_dir, exist_ok=True)
    os.makedirs(log_dir, exist_ok=True)
    os.makedirs(f"{log_dir}/eval", exist_ok=True)

    print(f"Creating {n_envs} parallel training environments...")
    env = SubprocVecEnv([make_env(terrain_type=terrain_type) for _ in range(n_envs)])
    env = VecNormalize(env, norm_obs=True, norm_reward=True, clip_obs=10.0)

    print("Creating evaluation environment...")
    eval_env = DummyVecEnv([make_env(terrain_type=terrain_type)])
    eval_env = VecNormalize(eval_env, norm_obs=True, norm_reward=False, clip_obs=10.0)

    checkpoint_callback = CheckpointCallback(
        save_freq=save_freq,
        save_path=save_dir,
        name_prefix="sac_checkpoint",
        save_vecnormalize=True,
    )

    eval_callback = EvalCallback(
        eval_env,
        best_model_save_path=f"{save_dir}/best",
        log_path=f"{log_dir}/eval",
        eval_freq=eval_freq,
        deterministic=True,
        render=False,
    )

    device = "cuda" if use_gpu and torch.cuda.is_available() else "cpu"
    print(f"Creating SAC model on device: {device}")
    model = SAC(
        "MlpPolicy",
        env,
        learning_rate=learning_rate,
        buffer_size=buffer_size,
        learning_starts=learning_starts,
        batch_size=batch_size,
        tau=tau,
        gamma=gamma,
        train_freq=train_freq,
        gradient_steps=gradient_steps,
        ent_coef=ent_coef,
        verbose=1,
        tensorboard_log=log_dir,
        device=device,
        policy_kwargs=dict(
            net_arch=dict(pi=[256, 256], qf=[256, 256]),
            activation_fn=torch.nn.ReLU,
        ),
    )

    print(f"Starting SAC training for {total_timesteps} timesteps...")
    model.learn(
        total_timesteps=total_timesteps,
        callback=[checkpoint_callback, eval_callback],
        progress_bar=True,
    )

    print(f"Saving final model to {save_dir}/sac_final")
    model.save(f"{save_dir}/sac_final")
    env.save(f"{save_dir}/sac_final_vecnormalize.pkl")

    print("Training complete!")
    return model, env


def main():
    parser = argparse.ArgumentParser(description="Train quadruped robot with RL")
    parser.add_argument(
        "--algo",
        type=str,
        default="ppo",
        choices=["ppo", "sac", "both"],
        help="Algorithm to use (ppo, sac, or both)",
    )
    parser.add_argument(
        "--timesteps",
        type=int,
        default=1_000_000,
        help="Total timesteps for training",
    )
    parser.add_argument(
        "--learning-rate",
        type=float,
        default=3e-4,
        help="Learning rate",
    )
    parser.add_argument(
        "--terrain",
        type=str,
        default="flat",
        choices=["flat", "planar_reflection", "terrain", "maze"],
        help="Terrain type",
    )
    parser.add_argument(
        "--save-dir",
        type=str,
        default="models",
        help="Directory to save models",
    )
    parser.add_argument(
        "--log-dir",
        type=str,
        default="logs",
        help="Directory to save logs",
    )
    parser.add_argument(
        "--n-envs",
        type=int,
        default=8,
        help="Number of parallel environments (default: 8, max: 16)",
    )
    parser.add_argument(
        "--cpu-only",
        action="store_true",
        help="Force CPU training even if GPU is available",
    )

    args = parser.parse_args()

    terrain_map = {
        "flat": TerrainType.FLAT,
        "planar_reflection": TerrainType.PLANAR_REFLECTION,
        "terrain": TerrainType.TERRAIN,
        "maze": TerrainType.MAZE,
    }
    terrain_type = terrain_map[args.terrain]

    use_gpu = not args.cpu_only and torch.cuda.is_available()

    print(f"\n{'='*50}")
    print(f"Training Configuration:")
    print(f"  Algorithm: {args.algo}")
    print(f"  Total timesteps: {args.timesteps:,}")
    print(f"  Learning rate: {args.learning_rate}")
    print(f"  Terrain: {args.terrain}")
    print(f"  Parallel environments: {args.n_envs}")
    print(f"  Device: {'CUDA (GPU)' if use_gpu else 'CPU'}")
    print(f"  CPU cores available: {os.cpu_count()}")
    print(f"{'='*50}\n")

    if args.algo == "ppo" or args.algo == "both":
        print("\n=== Training PPO ===\n")
        train_ppo(
            total_timesteps=args.timesteps,
            learning_rate=args.learning_rate,
            save_dir=f"{args.save_dir}/ppo",
            log_dir=f"{args.log_dir}/ppo",
            terrain_type=terrain_type,
            n_envs=args.n_envs,
            use_gpu=use_gpu,
        )

    if args.algo == "sac" or args.algo == "both":
        print("\n=== Training SAC ===\n")
        train_sac(
            total_timesteps=args.timesteps,
            learning_rate=args.learning_rate,
            save_dir=f"{args.save_dir}/sac",
            log_dir=f"{args.log_dir}/sac",
            terrain_type=terrain_type,
            n_envs=args.n_envs,
            use_gpu=use_gpu,
        )


if __name__ == "__main__":
    main()
