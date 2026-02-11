"use strict";

var emulator;

const R2_URL = "https://retrogamingsiteresource.dpdns.org";

// Game configurations
const GAMES = {
    'red-alert-2': {
        name: 'Red Alert 2',
        isoConfig: {
            url: R2_URL + '/game/yuri_cn.iso',
            size: 680587264,
        }
    },
    'starcraft': {
        name: 'StarCraft',
        isoConfig: {
            url: R2_URL + '/game/starcraft.iso',
            size: 306894848,
        }
    }
};

function startEmulator(gameId) {
    // Standard boot with the Driver ISO and State File
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
            size: 536870912,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
        },
        initial_state: { 
            url: R2_URL + "/windows98/states/windows98_audio_vga_2d.bin.zst",
        },
        acpi: false,
        network_relay_url: "wss://relay.widgetry.org/",
        preserve_fixed_proportions: true,
        boot_order: 0x213,
        audio: true,
        autostart: true
    });

    const game = GAMES[gameId];

    emulator.add_listener("emulator-ready", async () => {
        try {
            console.log("Calling set_cdrom now...");
            // 1. Wait for CD device attach
            const cdResponse = await fetch(game.isoConfig.url);
            if (!cdResponse.ok) {
                throw new Error("cd iso file not found");
            }

            let isoData = await cdResponse.arrayBuffer();
            await emulator.set_cdrom({
                buffer: isoData,
                async: false
            });
            isoData = null;
            console.log("CD device attached.");

        } catch (err) {
            console.error("App.js error:", err);
        }
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
    startEmulator(gameId);
}

// Initialize on page load
window.onload = function() {
    updateStatus("Click a game on the left to start");
    
    // Setup save state button
    document.getElementById("save_state").onclick = function() {
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
