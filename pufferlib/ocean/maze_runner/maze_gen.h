// maze_gen.h — perfect-maze generator (DFS backtracker)
// Grid encoding: 1 = wall, 0 = empty. Works for odd S >= 5.

#ifndef MAZE_GEN_H
#define MAZE_GEN_H

#include <stdint.h>
#include <string.h>   // memset
#include <stdlib.h>   // malloc, free

typedef struct { int r, c; } Cell;

static inline uint32_t xrsh(uint32_t *s){
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (*s = x ? x : 1u);
}
static inline int idx(int S, int r, int c){ return r*S + c; }

// helper: mark a cell empty and push onto stack
static inline void push_cell(uint8_t *walls, int S, Cell *stack, int *sp, int r, int c){
    walls[idx(S, r, c)] = 0;
    stack[(*sp)++] = (Cell){r, c};
}

// Generate a perfect maze into `walls` of size S*S (1=wall, 0=empty).
// Start cell at (1,1), goal cell at (S-2,S-2). Always solvable.
static inline void maze_generate(uint8_t *walls, int S, uint32_t *rng)
{
    if (S < 5) S = 5;
    if ((S & 1) == 0) S -= 1;  // force odd

    // 1) start with all walls
    memset(walls, 1, (size_t)S*(size_t)S);

    // 2) carve using iterative DFS on odd lattice cells
    int cap = (S*S)/2 + 8;
    Cell *stack = (Cell*)malloc((size_t)cap*sizeof(Cell));
    int sp = 0;

    push_cell(walls, S, stack, &sp, 1, 1);

    while (sp > 0){
        Cell cur = stack[sp-1];

        // unvisited neighbors 2 steps away (N,S,E,W) whose cells are still walls
        int nbrs_r[4], nbrs_c[4], n = 0;
        if (cur.r-2 > 0     && walls[idx(S,cur.r-2,cur.c)]==1){ nbrs_r[n]=cur.r-2; nbrs_c[n]=cur.c;   n++; }
        if (cur.r+2 < S-1   && walls[idx(S,cur.r+2,cur.c)]==1){ nbrs_r[n]=cur.r+2; nbrs_c[n]=cur.c;   n++; }
        if (cur.c-2 > 0     && walls[idx(S,cur.r,cur.c-2)]==1){ nbrs_r[n]=cur.r;   nbrs_c[n]=cur.c-2; n++; }
        if (cur.c+2 < S-1   && walls[idx(S,cur.r,cur.c+2)]==1){ nbrs_r[n]=cur.r;   nbrs_c[n]=cur.c+2; n++; }

        if (n == 0){ sp--; continue; } // backtrack

        // pick one neighbor at random
        int k = (int)(xrsh(rng) % (uint32_t)n);
        int nr = nbrs_r[k], nc = nbrs_c[k];

        // carve wall between cur and neighbor
        int wr = (cur.r + nr) >> 1;
        int wc = (cur.c + nc) >> 1;
        walls[idx(S,wr,wc)] = 0;

        // carve neighbor and continue
        push_cell(walls, S, stack, &sp, nr, nc);
    }

    free(stack);

    // 3) ensure start/goal cells are empty
    walls[idx(S,1,1)]         = 0;
    walls[idx(S,S-2,S-2)]     = 0;

    // (keep borders walled; open entrances if you want visuals)
    // walls[idx(S,0,1)] = 0;
    // walls[idx(S,S-1,S-2)] = 0;
}

#endif // MAZE_GEN_H
