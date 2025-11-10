#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include "raylib.h"


// Config
#define WIDTH 20
#define HEIGHT 10
#define CELL_SIZE 32
#define MAX_UNITS 128
#define MAX_TICKS 3600  // 60 seconds at 60fps

// Two-lane setup
#define LANES 2
#define LANE0_Y (HEIGHT/3)
#define LANE1_Y (HEIGHT - 1 - HEIGHT/3)

// Tile types
#define EMPTY 0
#define TOWER_PLAYER 1
#define TOWER_ENEMY 2
#define KNIGHT_PLAYER 3
#define ARCHER_PLAYER 4
#define TANK_PLAYER 5
#define KNIGHT_ENEMY 6
#define ARCHER_ENEMY 7
#define TANK_ENEMY 8
#define FLYING_PLAYER 9
#define FLYING_ENEMY  10
#define TILE_MAX 10

// Troop types
#define TROOP_KNIGHT 1
#define TROOP_ARCHER 2
#define TROOP_TANK   3
#define TROOP_FLYING 4

// Elixir
#define ELIXIR_START 10.0f
#define ELIXIR_MAX 10.0f
#define ELIXIR_REGEN 0.02f  // per frame

// Toggle to play as the enemy (team=1) with the keyboard
#ifndef ENEMY_MANUAL
#define ENEMY_MANUAL 0   // set to 0 to restore AI
#endif


// Types
typedef struct Log Log;
struct Log {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float n;
};

typedef struct Unit Unit;
struct Unit {
    int type;          // TROOP_*
    float x, y;        // position
    float health;
    float max_health;
    float damage;
    float speed;
    float range;
    float attack_cooldown;
    float attack_rate;
    int team;          // 0 = player, 1 = enemy
    int active;
    unsigned char lock_tower;
};

typedef struct RoyaleEnv RoyaleEnv;
struct RoyaleEnv {
    float* observations;
    float* actions;
    float* rewards;
    unsigned char* terminals;
    Log log;

    // Game state
    unsigned char* grid;
    Unit* units;
    int num_units;
    float tower_player_health;
    float tower_enemy_health;
    float elixir_player;
    float elixir_enemy;

    int tick;
    int cd;

    // Tower per-lane cooldowns and locked targets (-1 = none)
    float tower_cd_player[LANES];
    float tower_cd_enemy[LANES];
    int   tower_target_player[LANES];
    int   tower_target_enemy[LANES];

    // Sprite textures
    Texture2D knight_sprite;
    Texture2D archer_sprite;
    Texture2D golem_sprite;
    Texture2D dragon_sprite;
    bool sprites_loaded;

    // Config
    int width;
    int height;
    int obs_size;
    int length;  // For compatibility with binding.c

    // Action mask
    unsigned char* action_mask; // size 9: 0..8 (0=noop)
    int action_mask_size;       // = 9
};


// Troop stats

typedef struct TroopStats {
    float health;
    float damage;
    float speed;
    float range;
    float attack_rate;
    int cost;
} TroopStats;

// Added Flying unit (index 4)
static const TroopStats TROOP_DATA[] = {
    {0,   0,   0.0f, 0.0f,   0, 0},    // 0 = none
   //HP   DMG  SPEED  RANGE  ATK_RATE  COST
    {130, 1,  0.05f, 1.20f,    45,     3},  // 1 = Knight  (slight nerf: atk slower, a bit less HP)
    { 30,  1,  0.045f, 3.80f,    45,     2},  // 2 = Archer  (anti-air; a touch slower/weaker vs ground)
    {420, 100,  0.045f, 1.80f,    90,     5},  // 3 = Tank    (buffed: bigger HP/DMG/range; higher cost -> save pays off)
    {100, 30,  0.0625f, 2.00f,    45,     5},  // 4 = Flying  (buffed: faster/stronger; still loses hard to Archer via 2x bonus)
};


// Helpers for flying rules
static inline bool is_flying_type(int t) { return t == TROOP_FLYING; }
static inline bool can_attack_type(int attacker_type, int target_type) {
    // Knights & Tanks cannot hit air
    if ((attacker_type == TROOP_KNIGHT || attacker_type == TROOP_TANK) &&
        is_flying_type(target_type)) return false;
    // Archers & Flying can hit everything
    return true;
}


// Logging
void add_log(RoyaleEnv* env) {
    float score = env->tower_player_health - env->tower_enemy_health;
    env->log.episode_length += env->tick;
    env->log.episode_return += score;
    env->log.score += score;
    env->log.perf += (env->tower_player_health > env->tower_enemy_health) ? 1.0f : 0.0f;
    env->log.n += 1;
}

void init(RoyaleEnv* env) {
    env->tick = 0;
    env->width = WIDTH;
    env->height = HEIGHT;
    env->obs_size = WIDTH * HEIGHT + 4;  // grid + tower HPs + elixirs
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
}

void allocate(RoyaleEnv* env) {
    init(env);
    env->grid = (unsigned char*)calloc(WIDTH * HEIGHT, sizeof(unsigned char));
    env->units = (Unit*)calloc(MAX_UNITS, sizeof(Unit));
    env->observations = (float*)calloc(env->obs_size, sizeof(float));
    env->actions = (float*)calloc(1, sizeof(float));
    env->rewards = (float*)calloc(1, sizeof(float));
    env->terminals = (unsigned char*)calloc(1, sizeof(unsigned char));

    // action mask
    env->action_mask_size = 9;
    env->action_mask = (unsigned char*)calloc(env->action_mask_size, 1);
}

void free_allocated(RoyaleEnv* env) {
    free(env->grid);
    free(env->units);
    free(env->observations);
    free(env->actions);
    free(env->rewards);
    free(env->terminals);
    free(env->action_mask); 
}

void c_close(RoyaleEnv* env) {
    // Unload sprites if loaded
    if (env->sprites_loaded) {
        UnloadTexture(env->knight_sprite);
        UnloadTexture(env->archer_sprite);
        UnloadTexture(env->golem_sprite);
        UnloadTexture(env->dragon_sprite);
        env->sprites_loaded = false;
    }

    // Free environment-specific buffers
    if (env->grid) {
        free(env->grid);
        env->grid = NULL;
    }
    if (env->units) {
        free(env->units);
        env->units = NULL;
    }
    // Close raylib window if it's open
    if (IsWindowReady()) {
        CloseWindow();
    }
}


// Unit management
int spawn_unit(RoyaleEnv* env, int type, int team, int lane) {
    if (lane < 0 || lane >= LANES) return -1;
    if (env->num_units >= MAX_UNITS) return -1;

    const TroopStats* stats = &TROOP_DATA[type];

    // Check elixir
    float* elixir = (team == 0) ? &env->elixir_player : &env->elixir_enemy;
    if (*elixir < stats->cost) return -1;

    Unit* u = &env->units[env->num_units];
    u->type = type;
    u->health = stats->health;
    u->max_health = stats->health;
    u->damage = stats->damage;
    u->speed = stats->speed;
    u->range = stats->range;
    u->attack_rate = stats->attack_rate;
    u->attack_cooldown = 0;
    u->team = team;
    u->active = 1;
    u->lock_tower = 0;  

    // Spawn position
    u->x = (team == 0) ? 2.0f : (float)(WIDTH - 3);
    u->y = (lane == 0) ? (float)LANE0_Y : (float)LANE1_Y;

    *elixir -= stats->cost;
    env->num_units++;
    return env->num_units - 1;
}
static inline int random_affordable_enemy_type(RoyaleEnv* env){
    int candidates[4]; int n = 0;
    for (int t = 1; t <= 4; ++t){
        if (TROOP_DATA[t].cost <= 5 &&
            env->elixir_enemy >= (float)TROOP_DATA[t].cost){
            candidates[n++] = t;
        }
    }
    if (n == 0) return 0;              // nothing affordable → skip this tick
    return candidates[rand() % n];      // pick a cheap, affordable type
}

// Nearest enemy in same lane within range; -1 if none
static inline int acquire_tower_target(RoyaleEnv* env, int shooter_team,
                                       float tx, float ty, float range)
{
    int best = -1;
    float bestd = 1e9f;
    for (int i = 0; i < env->num_units; i++) {
        Unit *u = &env->units[i];
        if (!u->active || u->team == shooter_team) continue;      // enemy only
        if ((int)u->y != (int)ty) continue;                        // same lane

        float dx = u->x - tx, dy = u->y - ty;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d <= range && d < bestd) { bestd = d; best = i; }
    }
    return best;
}



// Action mask helpers 
static inline void update_action_mask(RoyaleEnv* env) {
    // 9 discrete actions: 0=noop,
    // lane 0: 1=K,2=A,3=T,4=F
    // lane 1: 5=K,6=A,7=T,8=F
    memset(env->action_mask, 0, env->action_mask_size);
    env->action_mask[0] = 1;  // noop always allowed

    if (env->num_units >= MAX_UNITS) return;

    float e = env->elixir_player;

    for (int t = 1; t <= 4; ++t) {
        int cost = TROOP_DATA[t].cost;
        unsigned char ok = (e >= (float)cost);
        env->action_mask[t]     = ok; // lane 0 (1..4)
        env->action_mask[4 + t] = ok; // lane 1 (5..8)
    }
}

static inline void set_terminal_mask(RoyaleEnv* env) {
    memset(env->action_mask, 0, env->action_mask_size);
    env->action_mask[0] = 1;  // only noop allowed on terminal frame
}


// Observations
void compute_observations(RoyaleEnv* env) {
    // Clear grid
    memset(env->grid, EMPTY, WIDTH * HEIGHT);

    // Single base per side, drawn once (center row)
    env->grid[(HEIGHT/2) * WIDTH + 1]         = TOWER_PLAYER;
    env->grid[(HEIGHT/2) * WIDTH + (WIDTH-2)] = TOWER_ENEMY;

    // Draw units (explicit mapping to support flying)
    for (int i = 0; i < env->num_units; i++) {
        Unit* u = &env->units[i];
        if (!u->active) continue;

        int gx = (int)u->x;
        int gy = (int)u->y;
        if (gx >= 0 && gx < WIDTH && gy >= 0 && gy < HEIGHT) {
            int tile = EMPTY;
            if (u->team == 0) {
                if      (u->type == TROOP_KNIGHT) tile = KNIGHT_PLAYER;
                else if (u->type == TROOP_ARCHER) tile = ARCHER_PLAYER;
                else if (u->type == TROOP_TANK)   tile = TANK_PLAYER;
                else if (u->type == TROOP_FLYING) tile = FLYING_PLAYER;
            } else {
                if      (u->type == TROOP_KNIGHT) tile = KNIGHT_ENEMY;
                else if (u->type == TROOP_ARCHER) tile = ARCHER_ENEMY;
                else if (u->type == TROOP_TANK)   tile = TANK_ENEMY;
                else if (u->type == TROOP_FLYING) tile = FLYING_ENEMY;
            }
            env->grid[gy * WIDTH + gx] = tile;  // y * WIDTH + x
        }
    }

    // Fill observations: normalized grid values
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        env->observations[i] = env->grid[i] / (float)TILE_MAX;
    }

    // Add tower health and elixir
    env->observations[WIDTH * HEIGHT + 0] = env->tower_player_health / 1000.0f;
    env->observations[WIDTH * HEIGHT + 1] = env->tower_enemy_health / 1000.0f;
    env->observations[WIDTH * HEIGHT + 2] = env->elixir_player / ELIXIR_MAX;
    env->observations[WIDTH * HEIGHT + 3] = env->elixir_enemy / ELIXIR_MAX;
}


// Reset
void c_reset(RoyaleEnv* env) {
    env->tick = 0;
    env->num_units = 0;
    env->tower_player_health = 1000.0f;
    env->tower_enemy_health = 1000.0f;
    env->elixir_player = ELIXIR_START;
    env->elixir_enemy = ELIXIR_START;
    env->cd = 0;

    for (int l = 0; l < LANES; l++) {
        env->tower_cd_player[l] = 0;
        env->tower_cd_enemy[l]  = 0;
        env->tower_target_player[l] = -1;
        env->tower_target_enemy[l]  = -1;
    }

    memset(env->grid, EMPTY, WIDTH * HEIGHT);
    memset(env->units, 0, MAX_UNITS * sizeof(Unit));

    compute_observations(env);
    update_action_mask(env); // keep mask consistent after reset (NEW)
}


// Step
void c_step(RoyaleEnv* env) {
    env->tick++;
    env->rewards[0] = 0.0f;
    env->terminals[0] = 0;
    float prev_elixir = env->elixir_player;  // store before regen/spending

    // Regenerate elixir
    if (env->elixir_player < ELIXIR_MAX) {
        env->elixir_player += ELIXIR_REGEN;
        if (env->elixir_player > ELIXIR_MAX) env->elixir_player = ELIXIR_MAX;
    }
    if (env->elixir_enemy < ELIXIR_MAX) {
        env->elixir_enemy += ELIXIR_REGEN;
        if (env->elixir_enemy > ELIXIR_MAX) env->elixir_enemy = ELIXIR_MAX;
    }

    // Player action:
    // 0=noop,
    // lane 0: 1=K,2=A,3=T,4=F
    // lane 1: 5=K,6=A,7=T,8=F
    // Player action every DECISION_PERIOD ticks
    if (env->cd <= 0) {
        int action = (int)(env->actions[0] + 0.5f);
        if (action >= 1 && action <= 8) {
            int lane = (action >= 5) ? 1 : 0;
            int type = 1 + ((action - 1) % 4);  // 1..4
            if(spawn_unit(env, type, 0, lane) == -1) {
                env->rewards[0] -= 0.05f;
            }
        }
        env->cd = 5;
    } else {
        env->cd--;
    }


    #if ENEMY_MANUAL
    // Enemy manual controls (team=1). Raylib input is fine to read here.
    // Lane 0 (top): U=Knight, I=Archer, O=Tank, P=Flying
    // Lane 1 (bot): J=Knight, K=Archer, L=Tank, ;=Flying
    int e_type = 0, e_lane = 0;

    if (IsKeyPressed(KEY_U)) { e_type = TROOP_KNIGHT; e_lane = 0; }
    else if (IsKeyPressed(KEY_I)) { e_type = TROOP_ARCHER; e_lane = 0; }
    else if (IsKeyPressed(KEY_O)) { e_type = TROOP_TANK;   e_lane = 0; }
    else if (IsKeyPressed(KEY_P)) { e_type = TROOP_FLYING; e_lane = 0; }

    else if (IsKeyPressed(KEY_J)) { e_type = TROOP_KNIGHT; e_lane = 1; }
    else if (IsKeyPressed(KEY_K)) { e_type = TROOP_ARCHER; e_lane = 1; }
    else if (IsKeyPressed(KEY_L)) { e_type = TROOP_TANK;   e_lane = 1; }
    else if (IsKeyPressed(KEY_SEMICOLON)) { e_type = TROOP_FLYING; e_lane = 1; }

    if (e_type > 0) {
        (void)spawn_unit(env, e_type, 1, e_lane); // respects enemy elixir
    }
    #else
        // Original enemy AI

        int t = random_affordable_enemy_type(env);
        if (t) {
            int lane = rand() & 1;
            (void)spawn_unit(env, t, 1, lane);  // spawn_unit already deducts elixir
        }
        
    #endif


    // Move and attack units
    for (int i = 0; i < env->num_units; i++) {
        Unit* u = &env->units[i];
        if (!u->active) continue;

        // Tower on same lane
        float tower_x = (u->team == 0) ? (WIDTH - 2.0f) : 1.0f;
        float tower_y = u->y;
        float dxT = u->x - tower_x, dyT = u->y - tower_y;
        float dist_to_tower = sqrtf(dxT*dxT + dyT*dyT);

        float nearest_dist;
        int   nearest_idx;

        if (u->lock_tower) {
            // Once locked, ignore new spawns
            nearest_idx  = -1;
            nearest_dist = dist_to_tower;
        } else {
            // Normal: prefer tower, but can switch to enemy if closer
            nearest_idx  = -1;
            nearest_dist = dist_to_tower;

            // Search enemies on same lane that this unit can attack
            for (int j = 0; j < env->num_units; j++) {
                Unit* other = &env->units[j];
                if (!other->active || other->team == u->team) continue;
                if ((int)other->y != (int)u->y) continue;
                if (!can_attack_type(u->type, other->type)) continue;

                float dx = u->x - other->x;
                float dy = u->y - other->y;
                float d  = sqrtf(dx*dx + dy*dy);
                if (d < nearest_dist) {
                    nearest_dist = d;
                    nearest_idx  = j;
                }
            }
        }

        // Attack or move
        if (nearest_dist <= u->range) {
            if (u->attack_cooldown <= 0) {
                if (nearest_idx == -1) {
                    // Hitting tower: lock permanently
                    u->lock_tower = 1; 
                    if (u->team == 0) {
                        env->tower_enemy_health -= u->damage;
                        env->rewards[0] += u->damage / 1000;
                    } else {
                        env->tower_player_health -= u->damage;
                        env->rewards[0] -= u->damage / 1000;
                    }
                } else {
                    // Hitting unit
                    float dmg = u->damage;
                    if (u->type == TROOP_ARCHER &&
                        env->units[nearest_idx].type == TROOP_FLYING) {
                    }
                    env->units[nearest_idx].health -= dmg;
                }
                u->attack_cooldown = u->attack_rate;
            } else {
                u->attack_cooldown--;
            }
        } else {
            // Move
            if (nearest_idx == -1) {
                // March toward tower if locked OR tower is current target
                if (u->team == 0) u->x += u->speed; else u->x -= u->speed;
            } else {
                // Move toward enemy (same lane, x-only)
                Unit* t = &env->units[nearest_idx];
                float dx = t->x - u->x;
                float adx = fabsf(dx);
                if (adx > 1e-6f) u->x += (dx / adx) * u->speed;
            }
        }

        // Clamp & keep on lane
        if (u->x < 0) u->x = 0;
        if (u->x >= WIDTH) u->x = WIDTH - 1;
        u->y = (fabsf(u->y - LANE0_Y) < fabsf(u->y - LANE1_Y))
            ? (float)LANE0_Y : (float)LANE1_Y;
    }

    // Tower attacks: lock-on until target dies 
    {
        const float t_range = TROOP_DATA[TROOP_ARCHER].range;
        const float t_rate  = 38;
        const float t_dmg   = TROOP_DATA[TROOP_ARCHER].health / 3.0f;

        // Player towers (team 0) shoot enemies (team 1)
        for (int l = 0; l < LANES; l++) {
            const float tx = 1.0f;
            const float ty = (l == 0) ? (float)LANE0_Y : (float)LANE1_Y;

            if (env->tower_cd_player[l] > 0) env->tower_cd_player[l]--;

            int t = env->tower_target_player[l];
            bool valid = false;
            if (t >= 0 && t < env->num_units) {
                Unit *u = &env->units[t];
                if (u->active && u->team == 1 && (int)u->y == (int)ty) {
                    float dx = u->x - tx, dy = u->y - ty;
                    float d  = sqrtf(dx*dx + dy*dy);
                    valid = (d <= t_range);
                }
            }
            if (!valid) {
                t = acquire_tower_target(env, /*shooter_team=*/0, tx, ty, t_range);
                env->tower_target_player[l] = t;
            }
            if (t != -1 && env->tower_cd_player[l] <= 0) {
                env->units[t].health -= t_dmg;
                env->tower_cd_player[l] = t_rate;
            }
        }

        // Enemy towers (team 1) shoot player units (team 0)
        for (int l = 0; l < LANES; l++) {
            const float tx = (float)(WIDTH - 2);
            const float ty = (l == 0) ? (float)LANE0_Y : (float)LANE1_Y;

            if (env->tower_cd_enemy[l] > 0) env->tower_cd_enemy[l]--;

            int t = env->tower_target_enemy[l];
            bool valid = false;
            if (t >= 0 && t < env->num_units) {
                Unit *u = &env->units[t];
                if (u->active && u->team == 0 && (int)u->y == (int)ty) {
                    float dx = u->x - tx, dy = u->y - ty;
                    float d  = sqrtf(dx*dx + dy*dy);
                    valid = (d <= t_range);
                }
            }
            if (!valid) {
                t = acquire_tower_target(env, /*shooter_team=*/1, tx, ty, t_range);
                env->tower_target_enemy[l] = t;
            }
            if (t != -1 && env->tower_cd_enemy[l] <= 0) {
                env->units[t].health -= t_dmg;
                env->tower_cd_enemy[l] = t_rate;
            }
        }
    }

    
 
    // Remove dead units and reward for kills
    for (int i = 0; i < env->num_units; i++) {
        Unit* u = &env->units[i];
        if (!u->active) continue;

        if (u->health <= 0) {
            u->active = 0;

            // Fetch the troop's elixir cost for scaling
            int cost = TROOP_DATA[u->type].cost;
            float reward = cost * 0.05f;  // ≈0.1 for archers, 0.15 for knights, 0.25 for tanks

            if (u->team == 1) {
                // Enemy died → reward player
               // env->rewards[0] += reward;
            } else if (u->team == 0) {
                // Our unit died → slight penalty
              //  env->rewards[0] -= reward * 0.5f;
            }
        }
    }


    // Check win conditions
    if (env->tower_enemy_health <= 0) {
        env->rewards[0] = 5.0f;
        env->terminals[0] = 1;
    } else if (env->tower_player_health <= 0) {
        env->rewards[0] = -5.0f;
        env->terminals[0] = 1;
    } else if (env->tick >= MAX_TICKS) {
        env->rewards[0] = (env->tower_player_health > env->tower_enemy_health) ? 0.1f : -0.1f;
        env->terminals[0] = 1;
    }

    
    //float elixir_change = env->elixir_player - prev_elixir;
    //env->rewards[0] += 0.1f * elixir_change;  // small reward for net saving

    if (env->terminals[0]) {
        // expose the true terminal observation + a safe mask
        compute_observations(env);
        set_terminal_mask(env);

        add_log(env);
        c_reset(env);
        return;
    }

    // normal step
    compute_observations(env);
    update_action_mask(env);
}


// Rendering

Color get_color(unsigned char tile) {
    switch(tile) {
        case EMPTY:         return (Color){20, 20, 30, 255};
        case TOWER_PLAYER:  return (Color){0, 200, 255, 255};
        case TOWER_ENEMY:   return (Color){255, 100, 100, 255};
        case KNIGHT_PLAYER: return (Color){0, 150, 255, 255};
        case ARCHER_PLAYER: return (Color){0, 255, 150, 255};
        case TANK_PLAYER:   return (Color){100, 100, 255, 255};
        case KNIGHT_ENEMY:  return (Color){255, 80, 80, 255};
        case ARCHER_ENEMY:  return (Color){255, 150, 80, 255};
        case TANK_ENEMY:    return (Color){255, 50, 50, 255};
        case FLYING_PLAYER: return (Color){120, 200, 255, 255};
        case FLYING_ENEMY:  return (Color){255, 200, 120, 255};
        default:            return (Color){128, 128, 128, 255};
    }
}

void c_render(RoyaleEnv* env) {
    // Initialize window on first call
    if (!IsWindowReady()) {
        InitWindow(WIDTH * CELL_SIZE, HEIGHT * CELL_SIZE + 100, "PufferLib Royale");
        SetTargetFPS(30);
    }

    // Load sprites on first render
    if (!env->sprites_loaded) {
        env->knight_sprite = LoadTexture("pufferlib/ocean/royale/assets/knight.png");
        env->archer_sprite = LoadTexture("pufferlib/ocean/royale/assets/archer.png");
        env->golem_sprite = LoadTexture("pufferlib/ocean/royale/assets/golem.png");
        env->dragon_sprite = LoadTexture("pufferlib/ocean/royale/assets/dragon.png");
        env->sprites_loaded = true;
    }

    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    int sz = CELL_SIZE;

    BeginDrawing();
    ClearBackground((Color){12, 14, 22, 255});

    // Draw grid (row-major indexing: y * WIDTH + x)
    // Only draw towers, skip unit tiles since we draw sprites instead
    for (int y = 0; y < env->height; y++) {
        for (int x = 0; x < env->width; x++) {
            unsigned char tile = env->grid[y * WIDTH + x];
            // Only draw tower tiles and empty background, skip unit tiles
            if (tile == TOWER_PLAYER || tile == TOWER_ENEMY || tile == EMPTY) {
                DrawRectangle(x * sz, y * sz, sz - 2, sz - 2, get_color(tile));
            }
        }
    }

    // Draw units with sprites (explicit mapping to support flying)
    for (int i = 0; i < env->num_units; i++) {
        Unit* u = &env->units[i];
        if (!u->active) continue;

        // Convert grid coordinates to pixel coordinates
        float px = u->x * sz;
        float py = u->y * sz;

        // Select sprite based on troop type
        Texture2D sprite;
        if (u->type == TROOP_KNIGHT) {
            sprite = env->knight_sprite;
        } else if (u->type == TROOP_ARCHER) {
            sprite = env->archer_sprite;
        } else if (u->type == TROOP_TANK) {
            sprite = env->golem_sprite;
        } else if (u->type == TROOP_FLYING) {
            sprite = env->dragon_sprite;
        } else {
            sprite = env->knight_sprite; // fallback
        }

        // Draw sprite (flip source rectangle for enemies to face left)
        Rectangle source;
        if (u->team == 0) {
            // Player: face right (normal)
            source = (Rectangle){0, 0, (float)sprite.width, (float)sprite.height};
        } else {
            // Enemy: face left (flip by reversing source x)
            source = (Rectangle){(float)sprite.width, 0, -(float)sprite.width, (float)sprite.height};
        }

        Rectangle dest = {px, py, (float)sz, (float)sz};
        DrawTexturePro(sprite, source, dest, (Vector2){0, 0}, 0, WHITE);

        // Health bar above unit
        float hp_pct = u->health / u->max_health;
        if (hp_pct < 0) hp_pct = 0;
        if (hp_pct > 1) hp_pct = 1;
        int bar_width = (int)(sz * hp_pct);

        // Draw background (red) then foreground (green)
        DrawRectangle((int)px, (int)py - 6, sz, 4, RED);
        DrawRectangle((int)px, (int)py - 6, bar_width, 4, GREEN);
    }

    // HUD at bottom
    int hud_y = HEIGHT * sz + 10;
    DrawText(TextFormat("Tower HP: %.0f", env->tower_player_health), 10, hud_y, 16, WHITE);
    DrawText(TextFormat("Enemy Tower: %.0f", env->tower_enemy_health), 10, hud_y + 20, 16, WHITE);
    DrawText(TextFormat("Elixir: %.1f", env->elixir_player), 10, hud_y + 40, 16, WHITE);
    DrawText(TextFormat("Units: %d", env->num_units), 10, hud_y + 60, 16, WHITE);
    DrawText("Actions: lane0 1-4=K/A/T/F, lane1 5-8=K/A/T/F", 250, hud_y, 14, GRAY);

    EndDrawing();
}

