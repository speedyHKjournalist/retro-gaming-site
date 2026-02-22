"use strict";

let emulator = null;

const R2_URL = "https://retrogamingsiteresource.dpdns.org";

// Game configurations
const GAMES = {
    'red-alert-2': {
        name: 'Red Alert 2',
        stateurl: '/windows98/states/windows98_audio_vga_2d_yuri_cn.bin.zst',
    },
    'starcraft': {
        name: 'StarCraft',
        stateurl: '/windows98/states/windows98_audio_vga_2d_starcraft.bin.zst',
    },
    'commando_1': {
        name: 'Commandos I',
        stateurl: '/windows98/states/windows98_audio_vga_2d_commando_1.bin.zst',
    },
    'Diablo_1': {
        name: 'Diablo 1',
        stateurl: '/windows98/states/windows98_audio_vga_2d_diablo.bin.zst',
    },
    'richman_4': {
        name: 'Richman 4 (大富翁4)',
        stateurl: '/windows98/states/windows98_audio_vga_2d_richman_4.bin.zst',
    },
    'rollercoaster_tycoon_2': {
        name: 'RollerCoaster Tycoon 2',
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img',
        systemDiskSize: 536870912,
        disk: '/game/rollercoaster2/rollercoaster2.img',
        size: 765460480,
        stateurl: '/windows98/states/windows98_audio_vga_2d_multidisk_rollercoaster2.bin.zst',
    },
    'counter_strike': {
        name: 'Counter-Strike 1.5',
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img',
        systemDiskSize: 536870912,
        disk: '/game/counterstrike/counterstrike.img',
        size: 943718400,
        stateurl: '/windows98/states/windows98_audio_vga_2d_multidisk_cs.bin.zst',
    },
    'fallout_2': {
        name: 'Fallout 2',
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img',
        systemDiskSize: 536870912,
        disk: '/game/fallout2/fallout2.img',
        size: 618659840,
        stateurl: '/windows98/states/windows98_audio_vga_2d_multidisk_fallout2.bin.zst',
    },
    'age_of_empires_2': {
        name: 'Age of Empires 2',
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img',
        systemDiskSize: 536870912,
        disk: '/game/ageofempires2/ageofempires2.img',
        size: 193986560,
        stateurl: '/windows98/states/windows98_audio_vga_2d_multidisk_ageofempires2.bin.zst',
    },
    'heros_3': {
        name: 'Heroes of Might and Magic 3',
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: '/game/heros3/heros3.img.zst',
        size: 408944640,
        stateurl: '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_heros3.bin.zst',
    },
    'civilization_2': {
        name: 'Civilization 2',
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img',
        systemDiskSize: 536870912,
        disk: '/game/civilization2/civilization2.img',
        size: 52428800,
        stateurl: '/windows98/states/windows98_audio_vga_2d_multidisk_civilization2.bin.zst',
    },
};

const progressContainer = document.getElementById("progress_container");
const progressBar = document.getElementById("progress_bar");
const statusText = document.getElementById("status_text");

function showProgress() {
    progressContainer.classList.remove("hidden");
    progressBar.style.width = "0%";
}

function updateProgress(percent, text) {
    progressBar.style.width = percent + "%";
    if (text) statusText.textContent = text;
}

function hideProgress() {
    progressContainer.classList.add("hidden");
}

function startEmulator9x(gameId) {

    const game = GAMES[gameId];

    if (emulator) {
        emulator.stop();
        emulator.destroy();
        emulator = null;
    }

    emulator = new V86({
        memory_size: 256 * 1024 * 1024,
        vga_memory_size: 16 * 1024 * 1024,
        bios: { url: "bios/seabios.bin" },
        vga_bios: { url: "bios/vgabios.bin" },
        wasm_path: "v86.wasm",
        screen_container: document.getElementById("screen_container"),
        hda: {
            url: R2_URL + "/windows98/windows98hdd/windows98hdd.img",
            async: true,
            size: 2147483648,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        initial_state: { 
            url: R2_URL + game.stateurl,
        },
        acpi: false,
        network_relay_url: "wss://relay.widgetry.org/",
        preserve_fixed_proportions: true,
        boot_order: 0x213,
        audio: true,
        autostart: true
    });
    attachEmulatorListeners(emulator);
}

function startEmulator9xMultiDisk(gameId) {

    const game = GAMES[gameId];

    if (emulator) {
        emulator.stop();
        emulator.destroy();
        emulator = null;
    }

    emulator = new V86({
        memory_size: 256 * 1024 * 1024,
        vga_memory_size: 16 * 1024 * 1024,
        bios: { url: "bios/seabios.bin" },
        vga_bios: { url: "bios/vgabios.bin" },
        wasm_path: "v86.wasm",
        screen_container: document.getElementById("screen_container"),
        hda: {
            url: R2_URL + game.systemDisk,
            async: true,
            size: game.systemDiskSize,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        hdb: {
            url: R2_URL + game.disk,
            async: true,
            size: game.size,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        initial_state: { 
            url: R2_URL + game.stateurl,
        },
        acpi: false,
        network_relay_url: "wss://relay.widgetry.org/",
        preserve_fixed_proportions: true,
        boot_order: 0x213,
        audio: true,
        autostart: true
    });
    attachEmulatorListeners(emulator);
}

function attachEmulatorListeners(emulator) {

    emulator.add_listener("download-progress", function(e) {

        showProgress();

        if (e.total) {
            const percent = Math.floor((e.loaded / e.total) * 100);
            updateProgress(percent, `Loading game resource files ... ${percent}%`);
        } else {
            updateProgress(0, `Loading game resource files ...`);
        }
    });

    emulator.add_listener("emulator-ready", function() {
        hideProgress();
        updateStatus("Emulator ready");
    });
}

// Game launcher function
function launchGame(gameId) {
    const game = GAMES[gameId];
    if (!game) {
        updateStatus("Game not found!");
        return;
    }
    
    updateStatus("Starting " + game.name + "...");
    startEmulator9x(gameId);
}

// Game launcher function
function launchGameMultiDisk(gameId) {
    const game = GAMES[gameId];
    if (!game) {
        updateStatus("Game not found!");
        return;
    }
    
    updateStatus("Starting " + game.name + "...");
    startEmulator9xMultiDisk(gameId);
}

// Initialize on page load
window.onload = function() {
    updateStatus("Click a game on the left to start");
    
    // Setup save state button
    document.getElementById("save_state").onclick = async function() {
        if (!emulator) {
            updateStatus("Emulator not running!");
            return;
        }
        updateStatus("Saving state...");
        emulator.save_state().then(function(state_data) {
            var blob = new Blob([state_data], { type: "application/octet-stream" });
            var url = window.URL.createObjectURL(blob);
            var a = document.createElement("a");
            a.href = url;
            a.download = "v86_state.bin";
            a.click();
            window.URL.revokeObjectURL(url);
            updateStatus("State Saved!");
        });
    };

    // Setup load state button
    document.getElementById("load_state_btn").onclick = function() {
        document.getElementById("load_state_file").click();
    };

    document.getElementById("load_state_file").onchange = function(e) {
        var file = e.target.files[0];
        if (!file || !emulator) return;
        updateStatus("Restoring state...");
        var reader = new FileReader();
        reader.onload = function(e) {
            emulator.restore_state(e.target.result).then(function() {
                updateStatus("State Restored!");
            });
        };
        reader.readAsArrayBuffer(file);
    };

    // Setup insert CD button
    document.getElementById("insert_cd_btn").onclick = function() {
        document.getElementById("insert_cd_file").click();
    };

    document.getElementById("insert_cd_file").onchange = function(e) {
        var file = e.target.files[0];
        if (!file || !emulator) return;

        updateStatus("Loading CD image...");

        var reader = new FileReader();
        reader.onerror = function() {
            console.error("Failed to read CD file");
            updateStatus("Swap Failed: Read Error");
        };
        reader.onload = function(ev) {
            var arrayBuffer = ev.target.result;
            updateStatus("Inserting CD...");
            emulator.set_cdrom({
                buffer: arrayBuffer,
                async: true
            }).then(function() {
                updateStatus("CD Inserted! Check My Computer.");
                console.log("Successfully swapped to:", file.name);
            }).catch(function(err) {
                console.error("Swap error:", err);
                updateStatus("Swap Failed: Check Console");
            });
        };
        reader.readAsArrayBuffer(file);
    };

    // Setup fullscreen button
    document.getElementById("fullscreen_btn").onclick = function() {
        var container = document.getElementById("screen_container");
        if (!document.fullscreenElement) {
            container.requestFullscreen().catch(function(err) {
                console.error("Fullscreen request failed:", err);
                updateStatus("Fullscreen not supported");
            });
        } else {
            document.exitFullscreen();
        }
    };

    // Update fullscreen button text when fullscreen changes
    document.addEventListener("fullscreenchange", function() {
        var btn = document.getElementById("fullscreen_btn");
        if (document.fullscreenElement) {
            btn.innerText = "Exit Fullscreen";
        } else {
            btn.innerText = "Fullscreen";
        }
    });

    // Setup mouse lock
    var canvas = document.querySelector("#screen_container canvas");
    if (canvas) {
        canvas.addEventListener("mousedown", function() {
            canvas.requestPointerLock();
        });
    }
};

function updateStatus(text) {
    document.getElementById("status_text").innerText = text;
}
