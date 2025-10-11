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

        # Royale: two lanes of length `size` + 4 tower slots
        obs_len = 2 * size + 4
        self.single_observation_space = gymnasium.spaces.Box(
            low=0, high=255, shape=(obs_len,), dtype=np.uint8
        )
        # Royale action space: 7 discrete actions
        self.single_action_space = gymnasium.spaces.Discrete(7)

        self.render_mode = render_mode
        self.num_agents = num_envs
        self.log_interval = log_interval

        super().__init__(buf)
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
    actions = np.random.randint(0, 7, (CACHE, N))

    i = 0
    import time

    start = time.time()
    while time.time() - start < 10:
        env.step(actions[i % CACHE])
        steps += N
        i += 1

    print("Royale SPS:", int(steps / (time.time() - start)))
