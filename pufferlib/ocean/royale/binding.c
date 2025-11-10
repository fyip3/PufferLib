#include "royale.h"
#define Env RoyaleEnv
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs){
  int L = unpack(kwargs, "length");
  env->length = (L > 0) ? L : 11;

  // Initialize environment state (but DON'T allocate obs/actions/rewards/terminals)
  env->tick = 0;
  env->width = WIDTH;
  env->height = HEIGHT;
  env->obs_size = WIDTH * HEIGHT + 4;
  env->num_units = 0;
  env->sprites_loaded = false;
  env->cd = 0;

  // Initialize tower arrays
  for (int i = 0; i < LANES; i++) {
    env->tower_cd_player[i] = 0;
    env->tower_cd_enemy[i] = 0;
    env->tower_target_player[i] = -1;
    env->tower_target_enemy[i] = -1;
  }

  memset(&env->log, 0, sizeof(Log));

  // Allocate environment-specific buffers (NOT the Python numpy arrays)
  env->grid = (unsigned char*)calloc(WIDTH * HEIGHT, sizeof(unsigned char));
  env->units = (Unit*)calloc(MAX_UNITS, sizeof(Unit));

  env->action_mask_size = 9;  // 0..8: 0=noop, lane0 1-4, lane1 5-8
  env->action_mask = (unsigned char*)calloc(env->action_mask_size, 1);

  return 0;
}
static int my_log(PyObject* dict, Log* log){
  assign_to_dict(dict, "perf", log->perf);
  assign_to_dict(dict, "score", log->score);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "episode_length", log->episode_length);
  return 0;
}
