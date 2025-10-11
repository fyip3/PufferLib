#include "royale.h"
#define Env RoyaleEnv
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs){
  int L = unpack(kwargs, "length");
  env->length = (L > 0) ? L : 11;
  return 0;
}
static int my_log(PyObject* dict, Log* log){
  assign_to_dict(dict, "perf", log->perf);
  assign_to_dict(dict, "score", log->score);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "episode_length", log->episode_length);
  return 0;
}
