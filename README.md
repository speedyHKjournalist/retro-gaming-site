# Retro Gaming Site

A browser-based retro gaming site built on v86, with OpenGL and legacy Direct3D/DirectDraw proxies rendered by v86’s optional WebGPU graphics bundle.

Build and sync the runtime before serving a fresh checkout; see [graphics runtime setup](glbridge/README.md).

## Site structure

- `index.html` + `library.js`: responsive game library. Cards are sorted by the current visitor's play count stored in `localStorage`.
- `game.html?id=<game-id>` + `app.js`: shared game detail/emulator page. Every game has its own stable URL without duplicating the v86 UI.
- Common emulator controls: save state, load state, insert CD, eject CD, and full screen.
- Diablo II and KartRider links from the game library enable the COM file-transfer UI automatically with `v8ft=1`.
- MapleStory uses `game/maplestory.img` as its secondary disk and exposes an account-registration link below the emulator.

For site-wide popularity across all visitors, replace the local play-count storage with a backend analytics endpoint while keeping the same sorting interface.

## MapleStory account registration service

The browser never connects directly to MariaDB. `register.html` posts to the same-origin Node endpoint, which validates the input, applies Cosmic-compatible password hashing, and inserts the account with a parameterized query.

1. Create a dedicated MariaDB user restricted to the web server host. It only needs `INSERT` on `cosmic.accounts`; do not use the MariaDB root account.
2. Copy `.env.example` to `.env` and set `COSMIC_DB_USER`, `COSMIC_DB_PASSWORD`, and the public HTTPS origin.
3. Install dependencies with `npm install`.
4. Start the site and registration endpoint together with `npm start`.

Example least-privilege database setup, replacing both placeholders:

```sql
CREATE USER 'retro_registration'@'<web-server-ip>' IDENTIFIED BY '<long-random-password>';
GRANT INSERT ON cosmic.accounts TO 'retro_registration'@'<web-server-ip>';
FLUSH PRIVILEGES;
```

Cosmic currently defaults to bcrypt password migration. The service therefore uses bcrypt cost 12 by default, matching Cosmic's registration code. If the deployed server has `BCRYPT_MIGRATION: false`, set `COSMIC_PASSWORD_ALGORITHM=sha512` before registering accounts.

MariaDB must not be exposed to arbitrary internet clients. Firewall port 3306 so only the web server can reach it, and serve the registration form over HTTPS.

Run the Node regression suite with:

```sh
npm test
```

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
MapleStory v83
Warcraft III
