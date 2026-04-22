# 3D Maze Collector (OpenGL 3.3)

## Game Idea
3D maze game where the player moves through walls and obstacles to collect all coins before the timer ends.

## Controls
- `W / A / S / D`: Move
- `Mouse`: Look around
- `R`: Restart
- `Tab`: Toggle cursor capture
- `ESC`: Exit

## Build & Run
```bash
cmake -S . -B build
cmake --build build --config Release
```

Run:
- Windows: `build/Release/CGFinal3DGame.exe`

## Required Assets
- `assets/textures/grass.jpg`
- `assets/textures/wall.png`
- `assets/textures/collectible.png`
- `assets/textures/player.png`
