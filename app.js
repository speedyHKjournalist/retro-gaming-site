"use strict";

var emulator;

// Game configurations
const GAMES = {
    'red-alert-2': {
        name: 'Red Alert 2',
        cdImage: 'game/yuri_cn.iso',
        statePath: 'windows98/states/windows98_audio_vga_2d_yuri_cn.bin.zst'
    },
    'starcraft': {
        name: 'StarCraft',
        cdImage: 'game/starcraft.iso',
        statePath: 'windows98/states/windows98_audio_vga_2d_starcraft.bin.zst'
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
            url: "windows98/windows98hdd/.img",
            async: true,
            fixed_chunk_size: 1024 * 1024,
            use_parts: true,
            size: 512 * 1024 * 1024 
        },
        initial_state: { 
           url: "windows98/states/windows98_audio_vga_2d.bin.zst" 
        },
        acpi: false,
        network_relay_url: "wss://relay.widgetry.org/",
        preserve_fixed_proportions: true,
        boot_order: 0x213,
        audio: true,
        autostart: true
    });

    emulator.add_listener("emulator-ready", function() {
        updateStatus("Running...");
        
        // Get game configuration
        const game = GAMES[gameId] || GAMES['red-alert-2'];
        
        updateStatus("Loading CD: " + game.name + "...");
        fetch(game.cdImage)
            .then(function(response) {
                if (!response.ok) throw new Error("Failed to fetch CD image");
                return response.arrayBuffer();
            })
            .then(function(arrayBuffer) {
                updateStatus("Inserting CD...");
                return emulator.set_cdrom({
                    buffer: arrayBuffer,
                    async: true
                });
            })
            .then(function() {
                updateStatus("CD Inserted! Restoring state...");
                console.log("Successfully inserted CD: " + game.name);
                return fetch(game.statePath);
            })
            .then(function(response) {
                if (!response.ok) throw new Error("Failed to fetch state file");
                return response.arrayBuffer();
            })
            .then(function(stateData) {
                updateStatus("Restoring saved state...");
                return emulator.restore_state(stateData);
            })
            .then(function() {
                updateStatus("State Restored! Ready to play " + game.name);
                console.log("Successfully restored state for " + game.name);
            })
            .catch(function(err) {
                console.error("Setup error:", err);
                updateStatus("Setup Failed: Check Console");
            });
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
