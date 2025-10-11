/* Maze: a sample single-agent grid env.
 * Use this as a tutorial and template for your first env.
 * See the Target env for a slightly more complex example.
 * Star PufferLib on GitHub to support. It really, really helps!
 */

#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "maze_gen.h"

const unsigned char NOOP = 0;
const unsigned char DOWN = 1;
const unsigned char UP = 2;
const unsigned char LEFT = 3;
const unsigned char RIGHT = 4;

const unsigned char EMPTY = 0;
const unsigned char WALL = 1;
const unsigned char AGENT = 2;
const unsigned char TARGET = 3;

// Required struct. Only use floats!
typedef struct {
    float perf; // Recommended 0-1 normalized single real number perf metric
    float score; // Recommended unnormalized single real number perf metric
    float episode_return; // Recommended metric: sum of agent rewards over episode
    float episode_length; // Recommended metric: number of steps of agent episode
    // Any extra fields you add here may be exported to Python in binding.c
    float n; // Required as the last field 
} Log;

// Required that you have some struct for your env
// Recommended that you name it the same as the env file
typedef struct {
    Log log; // Required field. Env binding code uses this to aggregate logs
    unsigned char* observations; // Required. You can use any obs type, but make sure it matches in Python!
    int* actions; // Required. int* for discrete/multidiscrete, float* for box
    float* rewards; // Required
    unsigned char* terminals; // Required. We don't yet have truncations as standard yet
    int size;
    int tick;
    int r;
    int c;
    uint8_t* walls;  
    uint32_t rng;  
} Maze;

void add_log(Maze* env) {
    env->log.perf += (env->rewards[0] > 0) ? 1 : 0;
    env->log.score += env->rewards[0];
    env->log.episode_length += env->tick;
    env->log.episode_return += env->rewards[0];
    env->log.n++;
}

// Required function
void c_reset(Maze* env) {
    int tiles = env->size*env->size;
    memset(env->observations, 0, tiles*sizeof(unsigned char));
    if (!env->walls) {
        env->walls = (uint8_t*)malloc((size_t)tiles);
        env->rng = (uint32_t)rand() | 1u; // simple seed
    }
    maze_generate(env->walls, env->size, &env->rng);
    for(int i = 0; i < tiles; i++) {
        if(env->walls[i] == 1) {
            env->observations[i] = 1;
        } else {
            env->observations[i] = 0;
        }
    }

    env->r = 1;
    env->c = 1;
    env->observations[env->r * env->size + env->c] = AGENT;

    env->tick = 0;
    int target_idx = (env->size - 2) * env->size + (env->size - 2);
    env->observations[target_idx] = TARGET;
}

// Required function
void c_step(Maze* env) {
    env->tick += 1;

    int action = env->actions[0];
    env->terminals[0] = 0;
    env->rewards[0] = 0;

    if ((action == DOWN  && env->walls[(env->r+1) * env->size + env->c] == 1) ||
        (action == UP    && env->walls[(env->r-1) * env->size + env->c] == 1) ||
        (action == RIGHT && env->walls[env->r * env->size + (env->c+1)] == 1) ||
        (action == LEFT  && env->walls[env->r * env->size + (env->c-1)] == 1)) {
        
        env->rewards[0] -= 0.05;   // penalty for bumping a wall
        return;
    }

    env->observations[env->r*env->size + env->c] = env->walls[env->r*env->size + env->c]; // reset old cell


    if (action == DOWN) {
        env->r += 1;
    } else if (action == RIGHT) {
        env->c += 1;
    } else if (action == UP) {
        env->r -= 1;
    } else if (action == LEFT) {
        env->c -= 1;
    }

    if (env->tick > 3*env->size) {
        env->terminals[0] = 1;
        env->rewards[0] = -1.0;
        add_log(env);
        c_reset(env);
        return;
    }

    int pos = env->r*env->size + env->c;
    if (env->observations[pos] == TARGET) {
        env->terminals[0] = 1;
        env->rewards[0] = 1.0;
        add_log(env);
        c_reset(env);
        return;
    } 

    env->observations[pos] = AGENT;
}

// Required function. Should handle creating the client on first call
void c_render(Maze* env) {
    if (!IsWindowReady()) {
        InitWindow(64*env->size, 64*env->size, "PufferLib Maze");
        SetTargetFPS(5);
    }

    // Standard across our envs so exiting is always the same
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }
    BeginDrawing();
    ClearBackground((Color){6,24,24,255});

    int px = GetScreenWidth() / env->size;
    for (int i = 0; i < env->size; i++) {
        for (int j = 0; j < env->size; j++) {
            uint8_t w = env->walls[idx(env->size, i, j)];
            Color col = w ? (Color){50,50,50,255} : (Color){200,200,200,255};
            // overlay agent/target from observations
            unsigned char cell = env->observations[i*env->size + j];
            if (cell == AGENT)  col = (Color){0,255,255,255};
            if (cell == TARGET) col = (Color){255,64,64,255};
            DrawRectangle(j*px, i*px, px, px, col);
        }
    }

    EndDrawing();
}

// Required function. Should clean up anything you allocated
// Do not free env->observations, actions, rewards, terminals
void c_close(Maze* env) {
    if(env->walls) {
        free(env->walls);
        env->walls = NULL;
    } 
    if (IsWindowReady()) {
        CloseWindow();
    }
}
