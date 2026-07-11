"use strict";

const CLICK_STORAGE_KEY = "retro-gaming-site:play-counts:v1";
const GAME_COVERS = window.RETRO_GAME_COVERS || {};
const GAMES_LIBRARY = {
    heros_3: ["Heroes of Might and Magic 3", "1999", "Strategy", "Windows XP"],
    "red-alert-2": ["Red Alert 2", "2000", "Strategy", "Windows 98"],
    yuri: ["Yuri's Revenge", "2001", "Strategy", "Windows 98"],
    baldurs_gate_2: ["Baldur's Gate 2", "2000", "RPG", "Windows XP"],
    diablo_2: ["Diablo 2", "2000", "Action RPG", "Windows XP"],
    theme_hospital: ["Theme Hospital", "1997", "Simulation", "Windows 98"],
    starcraft: ["StarCraft", "1998", "Strategy", "Windows 98"],
    commandos_1: ["Commandos I", "1998", "Tactics", "Windows 98"],
    Diablo_1: ["Diablo 1", "1997", "Action RPG", "Windows 98"],
    richman_4: ["Richman 4 (大富翁4)", "1998", "Board game", "Windows 98"],
    rollercoaster_tycoon_2: ["Rollercoaster Tycoon 2", "2002", "Simulation", "Windows 98"],
    fallout_2: ["Fallout 2", "1998", "RPG", "Windows 98"],
    age_of_empires_2: ["Age of Empires 2", "1999", "Strategy", "Windows 98"],
    civilization_2: ["Civilization 2", "1996", "Strategy", "Windows XP"],
    planescape_torment: ["Planescape: Torment", "1999", "RPG", "Windows XP"],
    simcity_3000: ["SimCity 3000", "1999", "Simulation", "Windows 98"],
    icewind_dale_1: ["Icewind Dale 1", "2000", "RPG", "Windows XP"],
    icewind_dale_2: ["Icewind Dale 2", "2002", "RPG", "Windows XP"],
    need_for_speed_3: ["Need for Speed 3", "1998", "Racing", "Windows 98"],
    dino_crisis: ["Dino Crisis", "2000", "Survival", "Windows 98"],
    resident_evil_2: ["Resident Evil 2", "1999", "Survival", "Windows 98"],
    metal_slug_1: ["Metal Slug 1", "1996", "Arcade", "Windows XP"],
    metal_slug_2: ["Metal Slug 2", "1998", "Arcade", "Windows XP"],
    metal_slug_3: ["Metal Slug 3", "2000", "Arcade", "Windows XP"],
    metal_slug_4: ["Metal Slug 4", "2002", "Arcade", "Windows XP"],
    metal_slug_5: ["Metal Slug 5", "2003", "Arcade", "Windows XP"],
    metal_slug_x: ["Metal Slug X", "1999", "Arcade", "Windows XP"],
    warcraft3: ["Warcraft III", "2002", "Strategy", "Windows XP"]
};

const COVER_PALETTES = [
    ["#2c351b", "#a4cf54"], ["#321c1b", "#d3614f"],
    ["#172f35", "#55abc2"], ["#312541", "#9c70d2"],
    ["#3b2c19", "#d79a48"], ["#15352d", "#49bf96"],
    ["#3b1c2b", "#cf5985"], ["#24284a", "#6f7ee0"]
];

function readPlayCounts() {
    try {
        const parsed = JSON.parse(localStorage.getItem(CLICK_STORAGE_KEY) || "{}");
        return parsed && typeof parsed === "object" ? parsed : {};
    } catch (err) {
        return {};
    }
}

function coverStyle(gameId) {
    let hash = 0;
    for (let i = 0; i < gameId.length; i += 1) hash = ((hash << 5) - hash + gameId.charCodeAt(i)) | 0;
    const palette = COVER_PALETTES[Math.abs(hash) % COVER_PALETTES.length];
    return `--cover-a:${palette[0]};--cover-b:${palette[1]}`;
}

function initials(name) {
    return name.replace(/[^A-Za-z0-9\s]/g, " ").trim().split(/\s+/).slice(0, 3)
        .map(function(word) { return word.charAt(0); }).join("").toUpperCase();
}

function createGameCard(gameId, data, count) {
    const [name, year, genre, platform] = data;
    const coverPath = GAME_COVERS[gameId];
    const card = document.createElement("article");
    card.className = "game-card";
    const link = document.createElement("a");
    link.className = "game-card-link";
    link.href = `game.html?id=${encodeURIComponent(gameId)}`;
    link.setAttribute("aria-label", `Play ${name}`);
    link.innerHTML = `
        <div class="game-cover${coverPath ? " has-image" : ""}" style="${coverStyle(gameId)}">
            ${coverPath ? `<img class="cover-image" src="${coverPath}" alt="" loading="lazy" decoding="async">` : ""}
            <span class="cover-badge">${platform}</span>
            ${coverPath ? "" : `<strong class="cover-initials">${initials(name)}</strong>`}
        </div>
        <div class="card-info">
            <h3></h3>
            <div class="card-meta"><span>${year} · ${genre}</span><span class="card-play">${count ? `${count} play${count === 1 ? "" : "s"}` : "New"}</span></div>
        </div>`;
    link.querySelector("h3").textContent = name;
    card.appendChild(link);
    return card;
}

function renderLibrary(filter) {
    const counts = readPlayCounts();
    const query = (filter || "").trim().toLowerCase();
    const entries = Object.entries(GAMES_LIBRARY)
        .filter(function(entry) { return !query || `${entry[1][0]} ${entry[1][2]}`.toLowerCase().includes(query); })
        .sort(function(a, b) {
            return Number(counts[b[0]] || 0) - Number(counts[a[0]] || 0) || a[1][0].localeCompare(b[1][0]);
        });
    const list = document.getElementById("games_list");
    list.replaceChildren(...entries.map(function(entry) {
        return createGameCard(entry[0], entry[1], Number(counts[entry[0]] || 0));
    }));
    document.getElementById("result_count").textContent = `${entries.length} shown`;
    document.getElementById("empty_state").classList.toggle("hidden", entries.length !== 0);
}

document.addEventListener("DOMContentLoaded", function() {
    document.getElementById("game_count").textContent = Object.keys(GAMES_LIBRARY).length;
    renderLibrary("");
    const search = document.getElementById("game_search");
    search.addEventListener("input", function() { renderLibrary(search.value); });
});
