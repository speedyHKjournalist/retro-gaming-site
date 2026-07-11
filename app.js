"use strict";

let emulator = null;
let v86gl = null;
let stateOperationInProgress = false;

const R2_URL_1 = "https://retrogamingsiteresource.dpdns.org";
const R2_URL_2 = "https://resource2.19930724.xyz";

// Game configurations
const GAMES = {
    'heros_3': {
        name: 'Heroes of Might and Magic 3',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_1 + '/game/heros3/heros3.img.zst',
        size: 408944640,
        stateurl: R2_URL_1 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_heros3.bin.zst',
    },
    'red-alert-2': {
        name: 'Red Alert 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/ra2/ra2.img.zst',
        size: 1363148800,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_ra2.bin.zst',
    },
    'yuri': {
        name: 'Yuri\'s Revenge',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/yuri/yuri.img.zst',
        size: 1363148800,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_yuri.bin.zst',
    },
    'baldurs_gate_2': {
        name: 'Baldur\'s Gate 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_1 + '/game/baldurgate2/baldurgate2.img.zst',
        size: 3221225472,
        stateurl: R2_URL_1 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_baldursgate2.bin.zst',
    },
    'diablo_2': {
        name: 'Diablo 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_1 + '/game/diablo2/diablo2.img.zst',
        size: 2222981120,
        stateurl: R2_URL_1 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_diablo2.bin.zst',
    },
    'theme_hospital': {
        name: 'Theme Hospital',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/themehospital/themehospital.img.zst',
        size: 293601280,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_themehospital.bin.zst',
    },
    'starcraft': {
        name: 'StarCraft',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/starcraft/starcraft.img.zst',
        size: 367001600,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_starcraft.bin.zst',
    },
    'commandos_1': {
        name: 'Commandos I',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/commandos1/commandos1.img.zst',
        size: 157286400,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_commandos1.bin.zst',
    },
    'Diablo_1': {
        name: 'Diablo 1',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/diablo1/diablo1.img.zst',
        size: 167772160,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_diablo1.bin.zst',
    },
    'richman_4': {
        name: 'Richman 4 (大富翁4)',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/richman4/richman4.img.zst',
        size: 314572800,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_richman4.bin.zst',
    },
    'rollercoaster_tycoon_2': {
        name: 'Rollercoaster Tycoon 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/rollercoaster2/rollercoaster2.img.zst',
        size: 765460480,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_rollercoaster2.bin.zst',
    },
    'fallout_2': {
        name: 'Fallout 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/fallout2/fallout2.img.zst',
        size: 618659840,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_fallout2.bin.zst',
    },
    'age_of_empires_2': {
        name: 'Age of Empires 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_1 + '/game/ageofempires2/ageofempires2.img.zst',
        size: 193986560,
        stateurl: R2_URL_1 + '/windows98/states/windows98_audio_vga_2d_multidisk_ageofempires2.bin.zst',
    },
    'civilization_2': {
        name: 'Civilization 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_1 + '/game/civilization2/civilization.img.zst',
        size: 52428800,
        stateurl: R2_URL_1 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_civilization2.bin.zst',
    },
    'planescape_torment': {
        name: 'Planescape: Torment',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/torment/torment.img.zst',
        size: 2621440000,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_torment.bin.zst',
    },
    'simcity_3000': {
        name: 'SimCity 3000',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_2 + '/game/simcity3000/simcity3000.img.zst',
        size: 1572864000,
        stateurl: R2_URL_2 + '/windows98/states/windows98_audio_vga_2d_multidisk_simcity3000.bin.zst',
    },
    'icewind_dale_1': {
        name: 'Icewind Dale 1',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/icewinddale1/icewinddale1.img.zst',
        size: 2621440000,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_icewinddale1.bin.zst',
    },
    'icewind_dale_2': {
        name: 'Icewind Dale 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/icewinddale2/icewinddale2.img.zst',
        size: 2097152000,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_icewinddale2.bin.zst',
    },
    'need_for_speed_3': {
        name: 'Need for Speed 3',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_2 + '/game/nfs3/nfs3.img.zst',
        size: 2097152000,
        stateurl: R2_URL_2 + '/windows98/states/windows98_audio_vga_2d_multidisk_nfs3.bin.zst',
    },
    'dino_crisis': {
        name: 'Dino Crisis',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_2 + '/game/dinocrisis/dinocrisis.img.zst',
        size: 2097152000,
        stateurl: R2_URL_2 + '/windows98/states/windows98_audio_vga_2d_multidisk_dinocrisis.bin.zst',
    },
    'resident_evil_2': {
        name: 'Resident Evil 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windows98/windows98multidisk/windows98hdd_C_512MB/windows98hdd_C_512MB.img.zst',
        systemDiskSize: 536870912,
        disk: R2_URL_2 + '/game/residentevil2/residentevil2.img.zst',
        size: 4294967296,
        stateurl: R2_URL_2 + '/windows98/states/windows98_audio_vga_2d_multidisk_residentevil2.bin.zst',
    },
    'metal_slug_1': {
        name: 'Metal Slug 1',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslug1.bin.zst',
    },
    'metal_slug_2': {
        name: 'Metal Slug 2',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslug2.bin.zst',
    },
    'metal_slug_3': {
        name: 'Metal Slug 3',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslug3.bin.zst',
    },
    'metal_slug_4': {
        name: 'Metal Slug 4',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslug4.bin.zst',
    },
    'metal_slug_5': {
        name: 'Metal Slug 5',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslug5.bin.zst',
    },
    'metal_slug_x': {
        name: 'Metal Slug X',
        memorySize: 256 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/metalslug/metalslug.img.zst',
        size: 1073741824,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_metalslugx.bin.zst',
    },
    'warcraft3': {
        name: 'Warcraft III',
        memorySize: 512 * 1024 * 1024,
        systemDisk: '/windowsxp/windowsxpmultidisk/windowsxp_multidisk_C_2G.img.zst',
        systemDiskSize: 2147483648,
        disk: R2_URL_2 + '/game/warcraft3/warcraft3.img.zst',
        size: 2634022912,
        stateurl: R2_URL_2 + '/windowsxp/states/windowsxp_audio_vga_2d_multidisk_warcraft3.bin.zst',
    }    
};

const progressContainer = document.getElementById("progress_container");
const progressBar = document.getElementById("progress_bar");
const statusText = document.getElementById("status_text");

function renderGamesList() {
    const gamesList = document.getElementById("games_list");
    if (!gamesList) return;

    gamesList.replaceChildren();

    Object.entries(GAMES).forEach(function([gameId, game]) {
        const gameItem = document.createElement("div");
        gameItem.className = "game-item";
        gameItem.dataset.game = gameId;

        const gameTitle = document.createElement("div");
        gameTitle.className = "game-title";
        gameTitle.textContent = game.name;

        const launchButton = document.createElement("button");
        launchButton.className = "launch-btn";
        launchButton.type = "button";
        launchButton.textContent = "Launch";
        launchButton.setAttribute("aria-label", "Launch " + game.name);
        launchButton.addEventListener("click", function() {
            launchGameMultiDisk(gameId);
        });

        gameItem.append(gameTitle, launchButton);
        gamesList.appendChild(gameItem);
    });
}

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

function startEmulator9xMultiDisk(gameId) {

    const game = GAMES[gameId];

    if (emulator) {
        emulator.stop();
        emulator.destroy();
        emulator = null;
        v86gl = null;

        const glCanvas = document.getElementById("v86gl_canvas");
        if (glCanvas) {
            glCanvas.style.display = "none";
        }
    }

    emulator = new V86({
        memory_size: game.memorySize,
        vga_memory_size: 16 * 1024 * 1024,
        bios: { url: "bios/seabios.bin" },
        vga_bios: { url: "bios/vgabios.bin" },
        wasm_path: "v86.wasm",
        screen_container: document.getElementById("screen_container"),
        hda: {
            url: R2_URL_1 + game.systemDisk,
            async: true,
            size: game.systemDiskSize,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        hdb: {
            url: game.disk,
            async: true,
            size: game.size,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        initial_state: {
            url: game.stateurl,
        },
        acpi: false,
        net_device: {
            type : "ne2k",
            relay_url: "wss://relay.widgetry.org/"
        },
        v86gl_pci: {
            port: 0xF100,
            maxBatchBytes: 16 * 1024 * 1024
        },
        preserve_fixed_proportions: true,
        boot_order: 0x213,
        audio: true,
        autostart: true
    });
    attachEmulatorListeners(emulator);
}

function attachEmulatorListeners(emulator) {
    const glCanvas = document.getElementById("v86gl_canvas");

    const installV86GLBridge =
        typeof installV86GLNetworkBridge === "function" ? installV86GLNetworkBridge : null;

    if (glCanvas && installV86GLBridge) {
        try {
            v86gl = installV86GLBridge(emulator, glCanvas, {
                gl4es: window.GL4ES,
            });
            window.v86gl = v86gl;
        } catch (err) {
            console.warn("Failed to start v86 GL network bridge:", err);
        }
    }

    emulator.add_listener("emulator-loaded", function() {
        const pci = emulator.v86 && emulator.v86.cpu && emulator.v86.cpu.devices.v86gl_pci;
        if (!pci) {
            console.error("[v86gl] PCI device is missing from this libv86.js build");
        }
    });

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
    renderGamesList();
    updateStatus("Click a game on the left to start");
    
    // Setup save state button
    document.getElementById("save_state").onclick = async function() {
        if (!emulator) {
            updateStatus("Emulator not running!");
            return;
        }
        if (stateOperationInProgress) {
            updateStatus("Another state operation is already running");
            return;
        }

        const button = this;
        const activeEmulator = emulator;
        const activeBridge = v86gl;
        const wasRunning = activeEmulator.is_running();
        stateOperationInProgress = true;
        button.disabled = true;
        updateStatus("Saving state...");
        try {
            if (wasRunning) {
                await activeEmulator.stop();
            }
            if (activeBridge && typeof activeBridge.prepareSaveState === "function") {
                const glState = activeBridge.prepareSaveState();
                const glMiB = (glState.bytes / 1024 / 1024).toFixed(1);
                updateStatus("Saving state (OpenGL checkpoint " + glMiB + " MiB)...");
            }

            const state_data = await activeEmulator.save_state();
            var blob = new Blob([state_data], { type: "application/octet-stream" });
            var url = window.URL.createObjectURL(blob);
            var a = document.createElement("a");
            a.href = url;
            a.download = "v86_state.bin";
            a.click();
            window.URL.revokeObjectURL(url);
            updateStatus("State Saved!");
        } catch (err) {
            console.error("Failed to save emulator state:", err);
            updateStatus("Save Failed: " + (err && err.message || err));
        } finally {
            button.disabled = false;
            if (wasRunning && emulator === activeEmulator) {
                try {
                    await activeEmulator.run();
                } catch (err) {
                    console.error("Failed to resume emulator after save:", err);
                }
            }
            stateOperationInProgress = false;
        }
    };

    // Setup load state button
    document.getElementById("load_state_btn").onclick = function() {
        document.getElementById("load_state_file").click();
    };

    document.getElementById("load_state_file").onchange = async function(e) {
        var file = e.target.files[0];
        if (!file || !emulator) return;
        if (stateOperationInProgress) {
            this.value = "";
            updateStatus("Another state operation is already running");
            return;
        }

        const input = this;
        const activeEmulator = emulator;
        const activeBridge = v86gl;
        const wasRunning = activeEmulator.is_running();
        let restorePrepared = false;
        stateOperationInProgress = true;
        updateStatus("Restoring state...");
        try {
            const stateData = await file.arrayBuffer();
            if (wasRunning) {
                await activeEmulator.stop();
            }
            if (activeBridge && typeof activeBridge.beginStateRestore === "function") {
                activeBridge.beginStateRestore();
                restorePrepared = true;
            }

            await activeEmulator.restore_state(stateData);
            if (activeBridge && typeof activeBridge.finishStateRestore === "function") {
                const result = await activeBridge.finishStateRestore();
                if (!result.hasGLState) {
                    throw new Error(
                        "This legacy snapshot has no OpenGL checkpoint; create a new save with the updated bridge"
                    );
                }
            }

            if (wasRunning && emulator === activeEmulator) {
                await activeEmulator.run();
            }
            updateStatus("State Restored!");
        } catch (err) {
            if (restorePrepared && activeBridge &&
                typeof activeBridge.cancelStateRestore === "function") {
                activeBridge.cancelStateRestore();
            }
            console.error("Failed to restore emulator state:", err);
            updateStatus("Restore Failed: " + (err && err.message || err));
            // A partially restored VM must stay paused; running it with an
            // incomplete WebGL reconstruction would recreate the corruption.
        } finally {
            stateOperationInProgress = false;
            input.value = "";
        }
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
