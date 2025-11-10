'''A simple sample environment. Use this as a template for your own envs (Royale variant).'''

import gymnasium
import numpy as np

import pufferlib
from pufferlib.ocean.royale import binding


class Royale(pufferlib.PufferEnv):
    def __init__(self, num_envs=1, render_mode=None, log_interval=128, size=11, buf=None, seed=0):
        if isinstance(size, str):
            size = int(size)
        if isinstance(num_envs, str):
            num_envs = int(num_envs)
        if isinstance(log_interval, str):
            log_interval = int(log_interval)

        # Royale: grid is WIDTH * HEIGHT + 4 (tower health + elixir)
        # WIDTH = 20, HEIGHT = 10, so obs = 200 + 4 = 204
        obs_len = 20 * 10 + 4
        self.single_observation_space = gymnasium.spaces.Box(
            low=0, high=1.0, shape=(obs_len,), dtype=np.float32
        )
        # Royale action space: 0=noop, 1=knight, 2=archer, 3=tank
        self.single_action_space = gymnasium.spaces.Discrete(9)

        self.render_mode = render_mode
        self.num_envs = int(num_envs)
        self.num_agents = num_envs
        self.log_interval = log_interval

        super().__init__(buf)
        # Convert actions to float32 to match C expectations
        self.actions = self.actions.astype(np.float32)

        # Must use 'length=size' to match C binding keyword
        self.c_envs = binding.vec_init(
            self.observations,
            self.actions,
            self.rewards,
            self.terminals,
            self.truncations,
            num_envs,
            seed,
            length=size,
        )

    def reset(self, seed=0):
        binding.vec_reset(self.c_envs, seed)
        self.tick = 0
        return self.observations, []

    def step(self, actions):
        self.tick += 1
        self.actions[:] = actions
        binding.vec_step(self.c_envs)

        info = []
        if self.tick % self.log_interval == 0:
            info.append(binding.vec_log(self.c_envs))

        return (
            self.observations,
            self.rewards,
            self.terminals,
            self.truncations,
            info,
        )

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)


if __name__ == "__main__":
    N = 4096
    env = Royale(num_envs=N)
    env.reset()
    steps = 0

    CACHE = 1024
    actions = np.random.randint(0, 9, (CACHE, N))  # 9 actions: 0-8

    i = 0
    import time

    start = time.time()
    while time.time() - start < 10:
        env.step(actions[i % CACHE])
        steps += N
        i += 1

    print("Royale SPS:", int(steps / (time.time() - start)))
