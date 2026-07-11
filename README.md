retrogaming.dpdns.org

A site for retrogaming based on Github V86 project.

## Site structure

- `index.html` + `library.js`: responsive game library. Cards are sorted by the current visitor's play count stored in `localStorage`.
- `game.html?id=<game-id>` + `app.js`: shared game detail/emulator page. Every game has its own stable URL without duplicating the v86 UI.
- Common emulator controls: save state, load state, insert CD, eject CD, and full screen.
- Game-specific controls: add entries in `populateGamePage()` in `app.js`. Diablo II currently exposes a `retro:game-action` event hook for future save-file integration.

For site-wide popularity across all visitors, replace the local play-count storage with a backend analytics endpoint while keeping the same sorting interface.

Games:
Heros Of Might and Magic III
Red Alert 2
Red Alert 2 Yuri's Revenge
Half Life
Theme Hospital
StarCraft I
Commandos I
Diablo I
Richman 4
Rollercoaster Tycoon 2
Fallout 2
Age Of Empire II
Civilization II
CounterStrike 1.5
