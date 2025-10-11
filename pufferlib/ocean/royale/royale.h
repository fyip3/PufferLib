#ifndef ROYALE_H
#define ROYALE_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Constants 
#define LANES 2
#define EMPTY 0
#define BOARD_X 48      
#define RIGHT_GUTTER 48 

// Player units
#define P_MELEE  1
#define P_RANGED 2
#define P_FLYING 3
// Enemy units
#define E_MELEE  11
#define E_RANGED 12
#define E_FLYING 13

// Animation / render
#define CELL_PX     48
#define ANIM_SPEED   8   // ticks per frame swap

// Smooth movement config
#ifndef RENDERS_PER_STEP
#define RENDERS_PER_STEP 15
#endif

// Gameplay
#define TOWER_HP_INIT    20
#define ENEMY_SPAWN_ODDS 1
#define MAX_TICKS_FACTOR 8

//  Logs / Env 
typedef struct {
    float perf, score, episode_return, episode_length, n;
} Log;

typedef struct {
    Log            log;
    unsigned char* observations;  // [2*L] + [2 base bytes]
    int*           actions;       // [1]
    float*         rewards;       // [1]
    unsigned char* terminals;     // [1]

    int length;
    int tick;
    int p_base;
    int e_base;

    // --- Clash flash (visual only): countdown per tile; size = 2*length ---
    unsigned char* flash;
} RoyaleEnv;

// ---- Smooth-move globals (no ABI change) ----
static unsigned char* g_prev = NULL; // size = 2*length
static int            g_prev_len = 0;
static float          g_lerp = 1.0f; // 0..1 interpolation progress

//  Helpers 
static inline int board_size(RoyaleEnv* env) { return 2 * env->length; }
static inline int obs_p_base(RoyaleEnv* env) { return board_size(env) + 0; }
static inline int obs_e_base(RoyaleEnv* env) { return board_size(env) + 1; }

static inline int is_player(unsigned char v) { return v > 0 && v < 10; }
static inline int is_enemy (unsigned char v) { return v >= 10; }
static inline unsigned char norm(unsigned char v){ return v ? (v>=10 ? v-10 : v) : 0; }

// RPS: melee(1) > ranged(2), ranged(2) > flying(3), flying(3) > melee(1)
static inline int beats(unsigned char a, unsigned char b){
    a = norm(a); b = norm(b);
    if (a == 0 || b == 0 || a == b) return 0;
    return (a == 1 && b == 2) || (a == 2 && b == 3) || (a == 3 && b == 1);
}

static inline void write_bases(RoyaleEnv* env) {
    env->observations[obs_p_base(env)] = env->p_base > 0 ? env->p_base : 0;
    env->observations[obs_e_base(env)] = env->e_base > 0 ? env->e_base : 0;
}
static inline void add_log(RoyaleEnv* env) {
    env->log.perf += (env->rewards[0] > 0);
    env->log.score += env->rewards[0];
    env->log.episode_length++;
    env->log.episode_return += env->rewards[0];
    env->log.n++;
}

//  Sprites 
static Texture2D tex_melee, tex_ranged, tex_flying;
static bool sprites_loaded = false;
static int  frame_w = 32, frame_h = 32;   // will be auto-detected from textures

static inline bool try_load(Texture2D* out, const char* a, const char* b, const char* c) {
    if (FileExists(a)) { *out = LoadTexture(a); return out->id != 0; }
    if (b && FileExists(b)) { *out = LoadTexture(b); return out->id != 0; }
    if (c && FileExists(c)) { *out = LoadTexture(c); return out->id != 0; }
    return false;
}
static inline void load_sprites_once(void) {
    if (sprites_loaded) return;

    bool okM = try_load(&tex_melee,
        "pufferlib/ocean/royale/assets/melee.png",
        "./pufferlib/ocean/royale/assets/melee.png",
        "assets/melee.png");
    bool okR = try_load(&tex_ranged,
        "pufferlib/ocean/royale/assets/ranged.png",
        "./pufferlib/ocean/royale/assets/ranged.png",
        "assets/ranged.png");
    bool okF = try_load(&tex_flying,
        "pufferlib/ocean/royale/assets/flying.png",
        "./pufferlib/ocean/royale/assets/flying.png",
        "assets/flying.png");

    sprites_loaded = okM && okR && okF;

    // Auto-detect frame size (assumes 2 frames horizontally)
    if (tex_melee.id != 0) {
        frame_w = tex_melee.width / 2;
        frame_h = tex_melee.height;
    }
}

//  Memory 
static inline void allocate(RoyaleEnv* env) {
    int obs_size = 2 * env->length + 2;
    env->observations = (unsigned char*)calloc(obs_size, sizeof(unsigned char));
    env->actions      = (int*)calloc(1, sizeof(int));
    env->rewards      = (float*)calloc(1, sizeof(float));
    env->terminals    = (unsigned char*)calloc(1, sizeof(unsigned char));
    env->flash        = (unsigned char*)calloc(2 * env->length, 1);  // for clash flash

    // allocate / resize global prev buffer
    int need = 2 * env->length;
    if (g_prev_len != need) {
        free(g_prev);
        g_prev = (unsigned char*)calloc(need, 1);
        g_prev_len = need;
    }
    g_lerp = 1.0f;
}
static inline void free_allocated(RoyaleEnv* env) {
    free(env->observations);
    free(env->actions);
    free(env->rewards);
    free(env->terminals);
    free(env->flash);

    env->observations = env->actions = env->rewards = env->terminals = NULL;
    env->flash = NULL;

    // free global prev buffer
    free(g_prev);
    g_prev = NULL;
    g_prev_len = 0;
    g_lerp = 1.0f;
}

//  Setup 
static inline void c_reset(RoyaleEnv* env) {
    memset(env->observations, 0, board_size(env) + 2);
    if (env->flash) memset(env->flash, 0, 2 * env->length);
    if (g_prev) memset(g_prev, 0, 2 * env->length);
    g_lerp = 1.0f;

    env->p_base = TOWER_HP_INIT;
    env->e_base = TOWER_HP_INIT;
    env->tick = 0;
    env->terminals[0] = 0;
    env->rewards[0] = 0;
    write_bases(env);
}

//  Spawns 
static inline void try_spawn_player(RoyaleEnv* env, int lane, unsigned char unit) {
    int idx = lane * env->length + 0;
    if (!env->observations[idx]) env->observations[idx] = unit;
}
static inline void maybe_spawn_enemy(RoyaleEnv* env, int lane) {
    if (ENEMY_SPAWN_ODDS <= 0) return;
    if (rand() % ENEMY_SPAWN_ODDS) return;
    int idx = lane * env->length + (env->length - 1);
    if (!env->observations[idx]) {
        int r = rand() % 3;
        env->observations[idx] = (r == 0) ? E_MELEE : (r == 1) ? E_RANGED : E_FLYING;
    }
}

//  Movement & Resolution 
// Prevents pass-through and allows sequential same-tick fights (chain resolution).
static inline void move_and_resolve(RoyaleEnv* env) {
    const int L = env->length;
    const int N = board_size(env);
    unsigned char *cur  = env->observations;
    unsigned char *next = (unsigned char*)calloc(N, 1);     // next board state

    for (int lane = 0; lane < 2; lane++) {
        // Per-lane scratch
        unsigned char *pprop = (unsigned char*)calloc(L, 1);  // player proposals
        unsigned char *eprop = (unsigned char*)calloc(L, 1);  // enemy proposals
        unsigned char *used  = (unsigned char*)calloc(L, 1);  // original positions consumed by a swap
        unsigned char *occ   = (unsigned char*)calloc(L, 1);  // occupants from resolved swaps (can be challenged)

        // ---------- Phase 1: resolve swap conflicts (P at i, E at i+1, both moving) ----------
        for (int i = 0; i < L-1; i++) {
            unsigned char P = cur[lane*L + i];
            unsigned char E = cur[lane*L + (i+1)];
            if (!is_player(P) || !is_enemy(E)) continue;

            bool p_will_move = (i < L-2);     // player holds in [L-2, L-1]
            bool e_will_move = (i+1 > 1);     // enemy holds in [0,1]
            if (!p_will_move || !e_will_move) continue; // not a swap attempt

            unsigned char np = norm(P), ne = norm(E);
            int winner = 0; // 0=mutual kill, 1=player wins, 2=enemy wins
            if (np == ne) winner = 0;
            else if (beats(P, E)) winner = 1;
            else if (beats(E, P)) winner = 2;

            if (winner == 1) {
                int dst = i+1;
                occ[dst] = P;                 // place winner as occupant (not final yet)
                if (env->flash) env->flash[lane*L + dst] = 10;
            } else if (winner == 2) {
                int dst = i;
                occ[dst] = E;
                if (env->flash) env->flash[lane*L + dst] = 10;
            } else {
                // mutual kill: flash both cells
                if (env->flash) {
                    env->flash[lane*L + i]   = 10;
                    env->flash[lane*L + i+1] = 10;
                }
            }
            used[i]   = 1;
            used[i+1] = 1;
            i++; // skip partner cell
        }

        // ---------- Phase 2: build proposals for remaining movers (skip consumed) ----------
        // Players (front to back so frontmost overwrites)
        for (int i = L - 1; i >= 0; i--) {
            if (used[i]) continue;
            unsigned char u = cur[lane*L + i];
            if (!is_player(u)) continue;
            int target = (i >= L - 2) ? i : i + 1;  // hold in stop zone
            pprop[target] = u;  // allow contesting occ[target] later
        }
        // Enemies (front to back so frontmost overwrites)
        for (int i = 0; i < L; i++) {
            if (used[i]) continue;
            unsigned char u = cur[lane*L + i];
            if (!is_enemy(u)) continue;
            int target = (i <= 1) ? i : i - 1;  // hold in stop zone
            eprop[target] = u;
        }

        // ---------- Phase 3: sequential per-tile resolution (including challenges vs occ) ----------
        for (int i = 0; i < L; i++) {
            int idx = lane*L + i;

            unsigned char survivor = occ[i];   // 0 if no swap winner
            bool fought = false;

            // If an occupant exists, let incoming opposite-side challengers fight it (in order).
            if (survivor) {
                // Player challenger?
                if (pprop[i] && is_enemy(survivor)) {
                    if (norm(pprop[i]) == norm(survivor)) {
                        survivor = 0; fought = true;
                    } else if (beats(pprop[i], survivor)) {
                        survivor = pprop[i]; fought = true;
                    } else if (beats(survivor, pprop[i])) {
                        fought = true; // survivor stays
                    }
                }
                // Enemy challenger?
                if (survivor && eprop[i] && is_player(survivor)) {
                    if (norm(eprop[i]) == norm(survivor)) {
                        survivor = 0; fought = true;
                    } else if (beats(eprop[i], survivor)) {
                        survivor = eprop[i]; fought = true;
                    } else if (beats(survivor, eprop[i])) {
                        fought = true; // survivor stays
                    }
                }

                next[idx] = survivor;
                if (fought && env->flash) env->flash[idx] = 10;
                continue;
            }

            // No occupant from swaps: resolve proposals as usual (and allow mutual kill).
            unsigned char P = pprop[i];
            unsigned char E = eprop[i];

            if (P && !E) {
                next[idx] = P;
            } else if (!P && E) {
                next[idx] = E;
            } else if (P && E) {
                unsigned char np = norm(P), ne = norm(E);
                if (np == ne) {
                    next[idx] = EMPTY;       // mutual kill
                    if (env->flash) env->flash[idx] = 10;
                } else if (beats(P, E)) {
                    next[idx] = P;
                    if (env->flash) env->flash[idx] = 10;
                } else if (beats(E, P)) {
                    next[idx] = E;
                    if (env->flash) env->flash[idx] = 10;
                } else {
                    next[idx] = EMPTY;       // safety fallback
                }
            } else {
                next[idx] = EMPTY;
            }
        }

        free(pprop);
        free(eprop);
        free(used);
        free(occ);
    }

    memcpy(cur, next, N);
    free(next);
}

//  Base damage 
static inline void base_attack_phase(RoyaleEnv* env){
    int L = env->length;
    unsigned char* b = env->observations;
    int dmgP = 0, dmgE = 0;

    for (int lane = 0; lane < 2; lane++){
        for (int i = (L >= 2 ? L - 2 : 0); i < L; i++) {
            if (is_player(b[lane * L + i])) {
                dmgP++;
                env->rewards[0] += 0.1;
            }

        }
        for (int i = 0; i < L && i <= 1; i++) {
            if (is_enemy(b[lane * L + i])) {
                dmgE++;
                env->rewards[0] -= 0.1;
            }
        }
    }

    env->e_base -= dmgP;
    env->p_base -= dmgE;
}

//  Step 
static inline void c_step(RoyaleEnv* env) {
    
    env->tick++;
    env->terminals[0] = 0;
    env->rewards[0]   = 0;

    // snapshot current board for interpolation (globals)
    if (g_prev && g_prev_len >= 2*env->length) {
        memcpy(g_prev, env->observations, 2 * env->length);
    }
    g_lerp = 0.0f;

    move_and_resolve(env);

    int a = env->actions[0];
    if      (a == 1) try_spawn_player(env, 0, P_MELEE);
    else if (a == 2) try_spawn_player(env, 0, P_RANGED);
    else if (a == 3) try_spawn_player(env, 0, P_FLYING);
    else if (a == 4) try_spawn_player(env, 1, P_MELEE);
    else if (a == 5) try_spawn_player(env, 1, P_RANGED);
    else if (a == 6) try_spawn_player(env, 1, P_FLYING);

    // enemy: at most one attempt per tick (random lane)
    if (ENEMY_SPAWN_ODDS > 0) {
        int lane = rand() & 1;
        maybe_spawn_enemy(env, lane);
    }

    base_attack_phase(env);
    write_bases(env);

    int max_ticks = env->length * MAX_TICKS_FACTOR;
    if      (env->e_base <= 0) { env->rewards[0] =  1; env->terminals[0] = 1; }
    else if (env->p_base <= 0) { env->rewards[0] = -1; env->terminals[0] = 1; }
    else if (env->tick >= max_ticks) { env->rewards[0] = -0.1f; env->terminals[0] = 1; }

    if (env->terminals[0]) { add_log(env); c_reset(env); }
}

//  Render 
static inline void c_render(RoyaleEnv* env) {
    int L  = env->length;
    int px = CELL_PX;

    // window width = left gutter + board + right gutter
    int width  = BOARD_X + px * L + RIGHT_GUTTER;
    int height = px * 2 + 64;

    if (!IsWindowReady()) {
        InitWindow(width, height, "Royale (Horizontal)");
        SetTargetFPS(60); // smoother visuals
        load_sprites_once();
    } else if (!sprites_loaded) {
        load_sprites_once();
    }

    if (IsKeyDown(KEY_ESCAPE)) exit(0);

    // advance interpolation toward 1 between steps (time-based)
    const float MOVE_TIME_SEC = 0.12f;
    if (g_lerp < 1.0f) {
        float da = GetFrameTime() / MOVE_TIME_SEC;
        g_lerp += da;
        if (g_lerp > 1.0f) g_lerp = 1.0f;
    }

    BeginDrawing();
    ClearBackground((Color){12,14,22,255});

    // board
    for (int lane = 0; lane < 2; lane++) {
        for (int i = 0; i < L; i++) {
            unsigned char v = env->observations[lane * L + i];

            // background tile
            DrawRectangle(BOARD_X + i * px, lane * px, px - 1, px - 1, (Color){28,30,40,255});

            // --- compute lerped X using globals ---
            float alpha = g_lerp;       // 0..1
            int j_prev = i, j_now = i;

            if (v != 0 && g_prev) {
                if (is_player(v)) {
                    if (i > 0 && g_prev[lane * L + (i - 1)] == v) j_prev = i - 1;
                } else if (is_enemy(v)) {
                    if (i + 1 < L && g_prev[lane * L + (i + 1)] == v) j_prev = i + 1;
                }
            }

            float j_lerped = (1.0f - alpha) * (float)j_prev + alpha * (float)j_now;
            int   x_px     = BOARD_X + (int)(j_lerped * px);
            int   y_px     = lane * px;

            // sprite or fallback
            bool drew_sprite = false;
            if (sprites_loaded) {
                Texture2D *tex = NULL;
                if (v == P_MELEE || v == E_MELEE)         tex = &tex_melee;
                else if (v == P_RANGED || v == E_RANGED)  tex = &tex_ranged;
                else if (v == P_FLYING || v == E_FLYING)  tex = &tex_flying;

                if (tex && tex->id != 0) {
                    int frame = (env->tick / ANIM_SPEED) % 2;
                    Rectangle src = (Rectangle){ frame * frame_w, 0, frame_w, frame_h };
                    if (v >= 10) { src.x = frame * frame_w + frame_w; src.width = -frame_w; }
                    Rectangle dest = (Rectangle){ x_px, y_px, px, px };
                    DrawTexturePro(*tex, src, dest, (Vector2){0,0}, 0, RAYWHITE);
                    drew_sprite = true;
                }
            }

            // fallback colored block if no sprite
            if (!drew_sprite && v != 0) {
                Color c = (Color){28,30,40,255};
                if (v == P_MELEE)  c = (Color){0,180,255,255};
                if (v == P_RANGED) c = (Color){0,255,180,255};
                if (v == P_FLYING) c = (Color){120,200,255,255};
                if (v == E_MELEE)  c = (Color){255,90,90,255};
                if (v == E_RANGED) c = (Color){255,160,90,255};
                if (v == E_FLYING) c = (Color){255,200,120,255};
                DrawRectangle(x_px + 6, y_px + 6, px - 12, px - 12, c);
            }

            // TEAM OUTLINE (player = cyan, enemy = red)
            if (v != 0) {
                Color outline = (v >= 10) ? (Color){255,120,120,255} : (Color){0,200,255,255};
                DrawRectangleLinesEx(
                    (Rectangle){ x_px + 1, y_px + 1, px - 2, px - 2 },
                    2.0f, outline);
            }

            // CLASH FLASH (pulsing yellow border where a fight resolved)
            int idx = lane * L + i;
            if (env->flash && env->flash[idx] > 0) {
                int a = 40 + env->flash[idx] * 20; if (a > 255) a = 255;
                Color flash = (Color){255, 220, 80, a};
                DrawRectangleLinesEx(
                    (Rectangle){ x_px + 3, y_px + 3, px - 6, px - 6 },
                    3.0f, flash);
            }
        }
    }

    int center_y = px - 6;
    DrawRectangle(BOARD_X - 20,          center_y, 12, 12, (Color){0,200,255,255}); // Player base (left of board)
    DrawRectangle(BOARD_X + px * L + 8,  center_y, 12, 12, (Color){255,120,120,255}); // Enemy base (right of board)

    // fade clash flashes
    if (env->flash) {
        int N = 2 * env->length;
        for (int k = 0; k < N; k++) if (env->flash[k] > 0) env->flash[k]--;
    }

    // HUD
    int hud_x = 8, hud_y = px * 2 + 12;
    DrawText("Agent Base:", hud_x, hud_y, 16, RAYWHITE);
    DrawRectangle(hud_x + 72, hud_y + 2,  (env->p_base > 0 ? env->p_base : 0) * 8, 10, (Color){0,200,255,255});
    DrawText("Enemy Base:", hud_x, hud_y + 22, 16, RAYWHITE);
    DrawRectangle(hud_x + 72, hud_y + 24, (env->e_base > 0 ? env->e_base : 0) * 8, 10, (Color){255,120,120,255});

    // Overlay a warning if sprites failed to load
    if (!sprites_loaded) {
        DrawText("Sprites not found. Showing fallbacks.",
                 width - 320, height - 22, 16, (Color){255,200,120,255});
    }

    EndDrawing();
}

//  Close 
static inline void c_close(RoyaleEnv* env) {
    (void)env;
    if (tex_melee.id)  UnloadTexture(tex_melee);
    if (tex_ranged.id) UnloadTexture(tex_ranged);
    if (tex_flying.id) UnloadTexture(tex_flying);
    if (IsWindowReady()) CloseWindow();
}

#ifdef __cplusplus
}
#endif
#endif
