# PufferLib Royale

A 2-lane tower defense game environment inspired by Clash Royale, built for reinforcement learning training with PufferLib.

## Overview

Royale is a competitive 1v1 game where players deploy troops to destroy the enemy tower while defending their own. The game features an elixir-based economy, multiple troop types, and strategic lane-based gameplay.

## Game Mechanics

### Map Layout
- **Grid:** 20x10 cells
- **Lanes:** 2 horizontal lanes (top and bottom)
- **Towers:** One base per player (player tower at x=1, enemy tower at x=18)

### Troops

| Troop   | HP  | Damage | Speed  | Range | Attack Rate | Cost |
|---------|-----|--------|--------|-------|-------------|------|
| Knight  | 130 | 1      | 0.05   | 1.2   | 45          | 3    |
| Archer  | 30  | 1      | 0.045  | 3.8   | 45          | 2    |
| Tank    | 420 | 100    | 0.045  | 1.8   | 90          | 5    |
| Flying  | 100 | 30     | 0.0625 | 2.0   | 45          | 5    |

**Special Mechanics:**
- Knights and Tanks cannot attack Flying units (ground-only)
- Archers can hit both ground and air targets
- Flying units can attack everything

### Elixir System
- Starting elixir: 10
- Maximum elixir: 10
- Regeneration: 0.02 per frame
- Troops cost elixir to deploy

### Towers
- Starting health: 1000 HP each
- Range: Same as Archer range (3.8)
- Damage: 1/3 of Archer HP per shot
- Attack rate: 38 frames
- Lock-on targeting: Towers maintain target until it dies or leaves range

## Action Space

9 discrete actions (1 action every 5 ticks):
- `0`: No-op
- Lane 0 (top): `1`=Knight, `2`=Archer, `3`=Tank, `4`=Flying
- Lane 1 (bottom): `5`=Knight, `6`=Archer, `7`=Tank, `8`=Flying

## Observation Space

204-dimensional vector:
- **Grid (200):** 20×10 grid with normalized tile values (0-1)
  - 0 = Empty
  - 1 = Player tower, 2 = Enemy tower
  - 3-5 = Player units (Knight/Archer/Tank)
  - 6-8 = Enemy units (Knight/Archer/Tank)
  - 9-10 = Flying units (Player/Enemy)
- **Tower Health (2):** Player and enemy tower HP (normalized by 1000)
- **Elixir (2):** Player and enemy elixir (normalized by 10)

## Rewards

- **Tower damage:** ±(damage/1000) for damaging enemy/player tower
- **Invalid action:** -0.05 for attempting illegal spawn
- **Win:** +5.0 for destroying enemy tower
- **Loss:** -5.0 for losing your tower
- **Timeout:** ±0.1 based on tower health difference

## Win Conditions

1. Enemy tower destroyed (player wins)
2. Player tower destroyed (player loses)
3. Timeout at 3600 ticks (~60 seconds at 60fps) - higher tower HP wins