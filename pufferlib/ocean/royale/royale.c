#include <time.h>
#include "royale.h"

int main(){
  RoyaleEnv env = {.length = 11};
  allocate(&env);
  srand((unsigned)time(NULL));
  c_reset(&env);

  while (!WindowShouldClose()){
    env.actions[0] = rand() % 7;
    c_step(&env);
    c_render(&env);
  }
  free_allocated(&env);
  c_close(&env);
  return 0;
}
