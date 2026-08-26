// v86 PCI DMA graphics router.
//
// OpenGL records are executed directly by gl-webgpu/gl_executor.js. D3D8 and
// D3D9 keep their tagged envelopes on the same v86gl.sys transport, but there
// is deliberately no GL4ES/WebGL decoder or fallback in this file.

(function(global) {
    "use strict";

    const V86GL_BRIDGE_VERSION = "gl-webgpu-only-v1-20260824";
    const CTRL_D3D8_BATCH = 0xFFE0;
    const CTRL_D3D9_BATCH = 0xFFE1;
    const EXTENDED_RECORD_SIZE = 0xFFFF;
    const PCI_STATE_GRAPHICS_INDEX = 8;
    const CHECKPOINT_MAGIC = 0x32534756; // "VGS2"
    const CHECKPOINT_VERSION = 1;
    const CHECKPOINT_HEADER_BYTES = 32;
    const DEFAULT_MAX_GL_JOURNAL_BYTES = 512 * 1024 * 1024;

    // Query/readback records do not contribute to reconstructing GL state.
    // Replaying them could overwrite restored guest DMA or leave a query open.
    const NON_REPLAYABLE_GL_OPS = new Set([
        94, 174, 175, 176, 177, 178, 179, 180, 181,
        188, 189, 190, 191, 192, 193, 211, 213, 216,
    ]);

    function u16(bytes, offset) {
        return bytes[offset] | bytes[offset + 1] << 8;
    }

    function u32(bytes, offset) {
        return (bytes[offset] | bytes[offset + 1] << 8 |
            bytes[offset + 2] << 16 | bytes[offset + 3] << 24) >>> 0;
    }

    function asBytes(value) {
        if (value instanceof Uint8Array) return value;
        if (ArrayBuffer.isView(value))
            return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        if (value instanceof ArrayBuffer) return new Uint8Array(value);
        return new Uint8Array(value || []);
    }

    function ownedBytes(value) {
        return asBytes(value).slice();
    }

    function styleSet(canvas, name, value, priority) {
        if (!canvas || !canvas.style) return;
        if (typeof canvas.style.setProperty === "function")
            canvas.style.setProperty(name, value, priority || "");
        else canvas.style[name] = value;
    }

    class V86GLNetworkBridge {
        constructor(emulator, canvas, options) {
            this.emulator = emulator;
            this.options = options || {};
            const documentCanvas = typeof document !== "undefined" ?
                document.getElementById("d3d_webgpu_canvas") : null;
            const shared = this.options.graphicsCanvas ||
                this.options.d3dCanvas || canvas || documentCanvas;
            this.canvas = shared;
            this.glCanvas = this.options.glCanvas || shared;
            this.d3d8Canvas = this.options.d3d8Canvas || shared;
            this.d3d9Canvas = this.options.d3d9Canvas || shared;
            this.container = shared && shared.parentElement;
            this.screenCanvas = this.findScreenCanvas();

            this.glExecutor = null;
            this.d3d8Executor = null;
            this.d3d9Executor = null;
            this.glSurface = this.emptySurface();
            this.d3d8Surface = this.emptySurface();
            this.d3d9Surface = this.emptySurface();
            this.activeOwner = null;
            this.d3d8BatchStreamSeen = false;
            this.d3d9BatchStreamSeen = false;
            this.d3d8OwnerSessionKey = null;
            this.d3d9OwnerSessionKey = null;
            this.sharedD3DCanvasConflictReported = false;
            this.d3d9SwapChainCanvases = new Map();

            this.glJournal = [];
            this.glJournalBytes = 0;
            this.glJournalOverflow = false;
            this.maxGLJournalBytes = this.options.maxGLJournalBytes ||
                DEFAULT_MAX_GL_JOURNAL_BYTES;
            this.replayingState = false;
            this.restoringState = false;
            this.restorePrepared = false;
            this.restoreSeen = false;
            this.restoreHadCheckpoint = false;
            this.pendingRestore = Promise.resolve();
            this.pendingBatches = [];
            this.pciStateDevice = null;

            this.installGLExecutor();
            this.installD3D8Executor();
            this.installD3D9Executor();

            if (emulator && typeof emulator.add_listener === "function") {
                emulator.add_listener("v86gl-pci-frame", event =>
                    this.pushPCIBatch(event));
                emulator.add_listener("emulator-loaded", () =>
                    this.attachPCIStateHooks());
            }
            this.attachPCIStateHooks();
        }

        emptySurface() {
            return { hwnd: 0, x: 0, y: 0, width: 0, height: 0,
                     displayWidth: 0, displayHeight: 0, visible: false };
        }

        installGLExecutor() {
            if (this.options.glExecutor) {
                this.glExecutor = this.options.glExecutor;
                return;
            }
            const install = this.options.installGLWebGPUExecutor ||
                global.installGLWebGPUExecutor;
            if (!this.glCanvas || typeof install !== "function") {
                console.error("[gl-webgpu] executor unavailable; load " +
                    "gl_executor.js and provide the shared graphics canvas");
                return;
            }
            const executorOptions = { ...(this.options.gl || {}) };
            const userSurface = executorOptions.onSurface;
            executorOptions.onSurface = (surface, reason) => {
                this.glSurface = { ...this.glSurface, ...surface };
                if (reason === "hide" || surface.visible === false) {
                    if (this.activeOwner === "gl") this.hideOverlayCanvas();
                } else {
                    this.showOwner("gl");
                }
                if (typeof userSurface === "function") userSurface(surface, reason);
            };
            executorOptions.writeGuestMemory = (offsetInBatch, data, metadata) => {
                if (!metadata || metadata.batchAddress === undefined) return;
                if (!this.emulator ||
                        typeof this.emulator.write_memory !== "function") return;
                this.emulator.write_memory(asBytes(data),
                    (metadata.batchAddress + offsetInBatch) >>> 0);
            };
            this.glExecutor = install(this.glCanvas, executorOptions);
        }

        installD3D8Executor() {
            if (this.options.d3d8Executor) {
                this.d3d8Executor = this.options.d3d8Executor;
                return;
            }
            const install = this.options.installD3D8WebGPUExecutor ||
                global.installD3D8WebGPUExecutor;
            if (!this.d3d8Canvas || typeof install !== "function") return;
            const opts = { ...(this.options.d3d8 || {}) };
            const userSurface = opts.onSurface;
            const userPresent = opts.onPresent;
            const userDestroy = opts.onDestroy;
            opts.onSurface = (surface, reason) => {
                const session = surface.sessionKey || null;
                if (reason === "hide" || surface.visible === false) {
                    if (!session || session === this.d3d8OwnerSessionKey) {
                        this.d3d8Surface = { ...this.d3d8Surface, ...surface };
                        if (this.activeOwner === "d3d8") this.hideOverlayCanvas();
                    }
                } else {
                    this.d3d8OwnerSessionKey = session;
                    this.d3d8Surface = { ...this.d3d8Surface, ...surface };
                    this.positionOwner("d3d8", false);
                }
                if (typeof userSurface === "function") userSurface(surface, reason);
            };
            opts.onPresent = (surface, stats) => {
                this.d3d8OwnerSessionKey = surface.sessionKey || null;
                this.d3d8Surface = { ...this.d3d8Surface, ...surface };
                if (surface.visible === false) this.hideOverlayCanvas();
                else this.showOwner("d3d8");
                if (typeof userPresent === "function") userPresent(surface, stats);
            };
            opts.onDestroy = (surface, reason) => {
                const session = surface.sessionKey || null;
                if (!session || session === this.d3d8OwnerSessionKey) {
                    this.d3d8OwnerSessionKey = null;
                    this.d3d8Surface = { ...this.d3d8Surface, ...surface,
                        visible: false };
                    if (this.activeOwner === "d3d8") this.hideOverlayCanvas();
                }
                if (typeof userDestroy === "function") userDestroy(surface, reason);
            };
            this.d3d8Executor = install(this.d3d8Canvas, opts);
        }

        installD3D9Executor() {
            if (this.options.d3d9Executor) {
                this.d3d9Executor = this.options.d3d9Executor;
                return;
            }
            const install = this.options.installD3D9WebGPUExecutor ||
                global.installD3D9WebGPUExecutor;
            if (!this.d3d9Canvas || typeof install !== "function") return;
            const opts = { ...(this.options.d3d9 || {}) };
            const userSurface = opts.onSurface;
            const userPresent = opts.onPresent;
            const userDestroy = opts.onDestroy;
            opts.onSurface = (surface, reason) => {
                const session = surface.sessionKey || null;
                if (reason === "hide" || surface.visible === false) {
                    if (!session || session === this.d3d9OwnerSessionKey) {
                        this.d3d9Surface = { ...this.d3d9Surface, ...surface };
                        if (this.activeOwner === "d3d9") this.hideOverlayCanvas();
                    }
                } else {
                    /* A CREATE/RESET only declares where a process would
                     * present; it does not make that process the visible
                     * owner. Capability helpers interleave their batches with
                     * games and dxdiag, so stealing ownership here made the
                     * live canvas disappear until the helper's (often absent)
                     * Present. Ownership changes only after a successful
                     * onPresent below. */
                    const hasOwner = this.d3d9OwnerSessionKey !== null;
                    const belongsToOwner = hasOwner &&
                        session === this.d3d9OwnerSessionKey;
                    const legacyOwner = !hasOwner && !session &&
                        this.activeOwner === "d3d9";
                    if (!hasOwner || belongsToOwner) {
                        this.d3d9Surface = { ...this.d3d9Surface, ...surface };
                        this.positionOwner("d3d9",
                            belongsToOwner || legacyOwner);
                    }
                }
                if (typeof userSurface === "function") userSurface(surface, reason);
            };
            opts.onPresent = (surface, stats) => {
                this.d3d9OwnerSessionKey = surface.sessionKey || null;
                this.d3d9Surface = { ...this.d3d9Surface, ...surface };
                if (surface.visible === false) this.hideOverlayCanvas();
                else this.showOwner("d3d9");
                if (typeof userPresent === "function") userPresent(surface, stats);
            };
            opts.onDestroy = (surface, reason) => {
                const session = surface.sessionKey || null;
                if (!session || session === this.d3d9OwnerSessionKey) {
                    this.d3d9OwnerSessionKey = null;
                    this.d3d9Surface = { ...this.d3d9Surface, ...surface,
                        visible: false };
                    if (this.activeOwner === "d3d9") this.hideOverlayCanvas();
                }
                if (typeof userDestroy === "function") userDestroy(surface, reason);
            };
            const userCreateSwapChainCanvas = opts.createSwapChainCanvas;
            opts.createSwapChainCanvas = surface => {
                if (typeof userCreateSwapChainCanvas === "function")
                    return userCreateSwapChainCanvas(surface);
                if (typeof document === "undefined" || !this.container) return null;
                const extra = document.createElement("canvas");
                extra.className = "v86gl-d3d9-swapchain-overlay";
                extra.width = Math.max(1, surface.width || 1);
                extra.height = Math.max(1, surface.height || 1);
                this.container.appendChild(extra);
                this.d3d9SwapChainCanvases.set(surface.swapChain, extra);
                return extra;
            };
            const userSwapChainSurface = opts.onSwapChainSurface;
            opts.onSwapChainSurface = (surface, reason) => {
                if (reason === "destroy") this.removeD3D9SwapChainCanvas(surface.swapChain);
                else this.positionD3D9SwapChainCanvas(surface);
                if (typeof userSwapChainSurface === "function")
                    userSwapChainSurface(surface, reason);
            };
            this.d3d9Executor = install(this.d3d9Canvas, opts);
        }

        pushPCIBatch(event) {
            const bytes = asBytes(event && event.bytes);
            if (this.restoringState) {
                this.pendingBatches.push({ ...event, bytes: bytes.slice() });
                return;
            }
            if (this.isEnvelope(bytes, CTRL_D3D8_BATCH))
                return this.pushD3D8PCIBatch(event, bytes);
            if (this.isEnvelope(bytes, CTRL_D3D9_BATCH))
                return this.pushD3D9PCIBatch(event, bytes);
            this.pushGLPCIBatch(event, bytes);
        }

        isEnvelope(bytes, opcode) {
            return bytes.byteLength >= 8 && u16(bytes, 0) === opcode &&
                u16(bytes, 2) === EXTENDED_RECORD_SIZE;
        }

        pushGLPCIBatch(event, bytes) {
            if (!this.glExecutor || typeof this.glExecutor.submit !== "function") {
                console.error("[gl-webgpu] executor unavailable");
                return;
            }
            if (!this.replayingState) this.recordGLBatch(bytes);
            this.glExecutor.submit(bytes, {
                pciFrameId: event.frameId >>> 0,
                submitCount: event.submitCount >>> 0,
                descriptorCommandCount: event.commandCount >>> 0,
                batchAddress: event.batchAddr === undefined ? undefined :
                    event.batchAddr >>> 0,
                responseBase: event.responseBase,
            });
            if (event.flags & 1) {
                this.glExecutor.onSwapBuffers();
                this.showOwner("gl");
            }
        }

        pushD3D8PCIBatch(event, bytes) {
            if (!this.d3d8Executor ||
                    typeof this.d3d8Executor.submit !== "function") {
                console.error("[d3d8-webgpu] executor unavailable");
                return;
            }
            const payloadBytes = u32(bytes, 4);
            if (payloadBytes !== bytes.byteLength - 8) {
                console.error("[d3d8-webgpu] malformed D8WG envelope");
                return;
            }
            this.warnOnSharedD3DCanvasConflict("d3d8");
            this.d3d8Executor.submit(bytes.subarray(8), {
                pciFrameId: event.frameId >>> 0,
                submitCount: event.submitCount >>> 0,
                descriptorCommandCount: event.commandCount >>> 0,
            });
        }

        pushD3D9PCIBatch(event, bytes) {
            if (!this.d3d9Executor ||
                    typeof this.d3d9Executor.submit !== "function") {
                console.error("[d3d9-webgpu] executor unavailable");
                return;
            }
            const payloadBytes = u32(bytes, 4);
            if (payloadBytes !== bytes.byteLength - 8) {
                console.error("[d3d9-webgpu] malformed D9WG envelope");
                return;
            }
            this.warnOnSharedD3DCanvasConflict("d3d9");
            const descriptorBase = event.descAddr >>> 0;
            const writeGuestMemory = (dmaOffset, data) => {
                const source = asBytes(data);
                const offset = dmaOffset >>> 0;
                const end = offset + source.byteLength;
                if (end < offset || end > 16 * 1024 * 1024)
                    throw new RangeError("D9WG response write is outside DMA memory");
                if (!this.emulator ||
                        typeof this.emulator.write_memory !== "function")
                    throw new Error("v86 physical-memory writer is unavailable");
                this.emulator.write_memory(source,
                    (descriptorBase + offset) >>> 0);
            };
            this.d3d9Executor.submit(bytes.subarray(8), {
                pciFrameId: event.frameId >>> 0,
                submitCount: event.submitCount >>> 0,
                descriptorCommandCount: event.commandCount >>> 0,
                descriptorBase,
                writeGuestMemory,
            });
        }

        recordGLBatch(bytes) {
            let offset = 0;
            const records = [];
            let added = 0;
            while (offset < bytes.byteLength) {
                const start = offset;
                if (offset + 4 > bytes.byteLength) return;
                const fn = u16(bytes, offset);
                let size = u16(bytes, offset + 2);
                offset += 4;
                if (size === EXTENDED_RECORD_SIZE) {
                    if (offset + 4 > bytes.byteLength) return;
                    size = u32(bytes, offset);
                    offset += 4;
                }
                if (size > bytes.byteLength - offset) return;
                offset += size;
                if (NON_REPLAYABLE_GL_OPS.has(fn)) continue;
                const record = bytes.slice(start, offset);
                records.push(record);
                added += record.byteLength;
            }
            if (this.glJournalBytes + added > this.maxGLJournalBytes) {
                this.glJournalOverflow = true;
                return;
            }
            this.glJournal.push(...records);
            this.glJournalBytes += added;
        }

        serializeCheckpoint() {
            if (this.restoringState || this.replayingState)
                throw new Error("graphics state is being restored");
            if (this.glJournalOverflow)
                throw new Error("OpenGL replay journal exceeded " +
                    Math.floor(this.maxGLJournalBytes / 1024 / 1024) + " MiB");
            const gl = new Uint8Array(this.glJournalBytes);
            let at = 0;
            for (const record of this.glJournal) {
                gl.set(record, at);
                at += record.byteLength;
            }
            const d3d8 = this.d3d8Executor &&
                    typeof this.d3d8Executor.serializeState === "function" ?
                ownedBytes(this.d3d8Executor.serializeState()) : new Uint8Array(0);
            const total = CHECKPOINT_HEADER_BYTES + gl.byteLength + d3d8.byteLength;
            const result = new Uint8Array(total);
            const view = new DataView(result.buffer);
            view.setUint32(0, CHECKPOINT_MAGIC, true);
            view.setUint16(4, CHECKPOINT_VERSION, true);
            view.setUint16(6, CHECKPOINT_HEADER_BYTES, true);
            view.setUint32(8, total, true);
            view.setUint32(12, gl.byteLength, true);
            view.setUint32(16, d3d8.byteLength, true);
            view.setUint32(20, this.glJournal.length, true);
            result.set(gl, CHECKPOINT_HEADER_BYTES);
            result.set(d3d8, CHECKPOINT_HEADER_BYTES + gl.byteLength);
            return result;
        }

        parseCheckpoint(checkpoint) {
            const bytes = asBytes(checkpoint);
            if (bytes.byteLength < CHECKPOINT_HEADER_BYTES)
                throw new Error("graphics checkpoint is truncated");
            const view = new DataView(bytes.buffer, bytes.byteOffset,
                bytes.byteLength);
            if (view.getUint32(0, true) !== CHECKPOINT_MAGIC ||
                    view.getUint16(4, true) !== CHECKPOINT_VERSION)
                throw new Error("graphics checkpoint version is unsupported");
            const header = view.getUint16(6, true);
            const total = view.getUint32(8, true);
            const glBytes = view.getUint32(12, true);
            const d3d8Bytes = view.getUint32(16, true);
            if (header < CHECKPOINT_HEADER_BYTES || total !== bytes.byteLength ||
                    header + glBytes + d3d8Bytes !== total)
                throw new Error("graphics checkpoint lengths are invalid");
            return {
                gl: bytes.slice(header, header + glBytes),
                d3d8: bytes.slice(header + glBytes, total),
            };
        }

        getPCIDevice() {
            const runtime = this.emulator && this.emulator.v86;
            const cpu = runtime && runtime.cpu;
            return cpu && cpu.devices && cpu.devices.v86gl_pci || null;
        }

        attachPCIStateHooks() {
            const device = this.getPCIDevice();
            if (!device) return false;
            if (this.pciStateDevice === device) return true;
            if (device.__v86glStateBridge && device.__v86glStateBridge !== this)
                return false;
            if (typeof device.get_state !== "function" ||
                    typeof device.set_state !== "function") return false;
            const bridge = this;
            const originalGetState = device.get_state;
            const originalSetState = device.set_state;
            device.get_state = function() {
                const state = originalGetState.call(this);
                state[PCI_STATE_GRAPHICS_INDEX] = bridge.serializeCheckpoint();
                return state;
            };
            device.set_state = function(state) {
                originalSetState.call(this, state);
                bridge.onPCIStateRestored(state &&
                    state[PCI_STATE_GRAPHICS_INDEX]);
            };
            device.__v86glStateBridge = this;
            this.pciStateDevice = device;
            return true;
        }

        prepareSaveState() {
            if (!this.attachPCIStateHooks())
                throw new Error("v86gl PCI device is not ready for save state");
            const checkpoint = this.serializeCheckpoint();
            return { entries: this.glJournal.length, bytes: checkpoint.byteLength };
        }

        beginStateRestore() {
            if (!this.attachPCIStateHooks())
                throw new Error("v86gl PCI device is not ready for state restore");
            this.restorePrepared = true;
            this.restoreSeen = false;
            this.restoreHadCheckpoint = false;
            this.restoringState = true;
            this.pendingRestore = Promise.resolve();
        }

        onPCIStateRestored(checkpoint) {
            this.restoreSeen = true;
            this.restoreHadCheckpoint = !!(checkpoint && checkpoint.byteLength);
            this.restoringState = true;
            this.pendingRestore = Promise.resolve().then(() =>
                this.restoreCheckpoint(checkpoint));
            this.pendingRestore.catch(error =>
                console.error("[v86gl] graphics state restore failed", error));
        }

        async restoreCheckpoint(checkpoint) {
            const parsed = checkpoint && checkpoint.byteLength ?
                this.parseCheckpoint(checkpoint) :
                { gl: new Uint8Array(0), d3d8: new Uint8Array(0) };
            if (!this.glExecutor ||
                    typeof this.glExecutor.resetForReplay !== "function")
                throw new Error("GL WebGPU executor cannot reset for replay");
            this.replayingState = true;
            try {
                this.glExecutor.resetForReplay();
                this.glJournal = parsed.gl.byteLength ? [parsed.gl.slice()] : [];
                this.glJournalBytes = parsed.gl.byteLength;
                this.glJournalOverflow = false;
                if (parsed.gl.byteLength)
                    this.glExecutor.submit(parsed.gl, { replay: true });
                if (parsed.gl.byteLength &&
                        typeof this.glExecutor.onSwapBuffers === "function")
                    this.glExecutor.onSwapBuffers();
                if (parsed.d3d8.byteLength && this.d3d8Executor &&
                        typeof this.d3d8Executor.restoreState === "function")
                    await this.d3d8Executor.restoreState(parsed.d3d8);
            } finally {
                this.replayingState = false;
                this.restoringState = false;
                this.drainPendingBatches();
            }
        }

        async finishStateRestore() {
            if (!this.restoreSeen) this.onPCIStateRestored(null);
            await this.pendingRestore;
            this.restorePrepared = false;
            return { hasGLState: this.restoreHadCheckpoint };
        }

        cancelStateRestore() {
            this.restorePrepared = false;
            this.restoreSeen = false;
            this.restoreHadCheckpoint = false;
            this.replayingState = false;
            this.restoringState = false;
            this.pendingRestore = Promise.resolve();
            this.drainPendingBatches();
        }

        drainPendingBatches() {
            const pending = this.pendingBatches.splice(0);
            for (const event of pending) this.pushPCIBatch(event);
        }

        findScreenCanvas() {
            if (!this.container ||
                    typeof this.container.getElementsByTagName !== "function")
                return null;
            const overlays = new Set([this.canvas, this.glCanvas,
                this.d3d8Canvas, this.d3d9Canvas]);
            const canvases = this.container.getElementsByTagName("canvas");
            for (let i = 0; i < canvases.length; ++i)
                if (!overlays.has(canvases[i])) return canvases[i];
            return null;
        }

        ownerCanvas(owner) {
            if (owner === "d3d8") return this.d3d8Canvas;
            if (owner === "d3d9") return this.d3d9Canvas;
            return this.glCanvas;
        }

        ownerSurface(owner) {
            if (owner === "d3d8") return this.d3d8Surface;
            if (owner === "d3d9") return this.d3d9Surface;
            return this.glSurface;
        }

        styleOverlayCanvas(canvas, left, top, width, height, visible) {
            if (!canvas || !canvas.style) return;
            canvas.style.position = "absolute";
            canvas.style.left = left + "px";
            canvas.style.top = top + "px";
            styleSet(canvas, "width", width + "px", "important");
            styleSet(canvas, "height", height + "px", "important");
            styleSet(canvas, "max-width", "none", "important");
            styleSet(canvas, "max-height", "none", "important");
            canvas.style.pointerEvents = "none";
            canvas.style.display = visible ? "block" : "none";
            canvas.style.visibility = visible ? "visible" : "hidden";
        }

        ownerRect(owner) {
            const canvas = this.ownerCanvas(owner);
            const surface = this.ownerSurface(owner);
            const w = surface.displayWidth || surface.width || canvas.width || 1;
            const h = surface.displayHeight || surface.height || canvas.height || 1;
            let left = surface.x || 0;
            let top = surface.y || 0;
            let width = w;
            let height = h;
            if (this.container && this.screenCanvas && this.screenCanvas.width &&
                    this.screenCanvas.height &&
                    typeof this.container.getBoundingClientRect === "function" &&
                    typeof this.screenCanvas.getBoundingClientRect === "function") {
                const containerRect = this.container.getBoundingClientRect();
                const screenRect = this.screenCanvas.getBoundingClientRect();
                const scaleX = screenRect.width / this.screenCanvas.width;
                const scaleY = screenRect.height / this.screenCanvas.height;
                left = screenRect.left - containerRect.left + left * scaleX;
                top = screenRect.top - containerRect.top + top * scaleY;
                width *= scaleX;
                height *= scaleY;
            }
            return { left, top, width, height };
        }

        positionOwner(owner, visible) {
            const canvas = this.ownerCanvas(owner);
            if (!canvas) return;
            const rect = this.ownerRect(owner);
            this.styleOverlayCanvas(canvas, rect.left, rect.top,
                rect.width, rect.height,
                visible === undefined ? this.activeOwner === owner : visible);
        }

        showOwner(owner) {
            this.activeOwner = owner;
            const active = this.ownerCanvas(owner);
            for (const canvas of new Set([this.glCanvas, this.d3d8Canvas,
                    this.d3d9Canvas])) {
                if (canvas && canvas !== active)
                    this.styleOverlayCanvas(canvas, 0, 0, 0, 0, false);
            }
            this.positionOwner(owner, true);
        }

        positionCanvas() {
            if (this.activeOwner) this.positionOwner(this.activeOwner, true);
        }

        positionGLCanvas() { this.positionOwner("gl"); }
        positionD3D8Canvas() { this.positionOwner("d3d8"); }
        positionD3D9Canvas() { this.positionOwner("d3d9"); }

        hideOverlayCanvas() {
            for (const canvas of new Set([this.glCanvas, this.d3d8Canvas,
                    this.d3d9Canvas]))
                if (canvas) this.styleOverlayCanvas(canvas, 0, 0, 0, 0, false);
            this.activeOwner = null;
        }

        showOverlayCanvas() { this.showOwner("gl"); }
        hideGLCanvas() {
            if (this.activeOwner === "gl") this.hideOverlayCanvas();
        }
        showD3D8Canvas() { this.showOwner("d3d8"); }
        hideD3D8Canvas() {
            if (this.activeOwner === "d3d8") this.hideOverlayCanvas();
        }
        showD3D9Canvas() { this.showOwner("d3d9"); }
        hideD3D9Canvas() {
            if (this.activeOwner === "d3d9") this.hideOverlayCanvas();
        }

        positionD3D9SwapChainCanvas(surface) {
            const canvas = this.d3d9SwapChainCanvases.get(surface.swapChain);
            if (!canvas) return;
            const saved = this.d3d9Surface;
            this.d3d9Surface = { ...saved, ...surface };
            const rect = this.ownerRect("d3d9");
            this.d3d9Surface = saved;
            this.styleOverlayCanvas(canvas, rect.left, rect.top,
                rect.width, rect.height, surface.visible !== false);
        }

        removeD3D9SwapChainCanvas(handle) {
            const canvas = this.d3d9SwapChainCanvases.get(handle);
            if (!canvas) return;
            this.d3d9SwapChainCanvases.delete(handle);
            if (canvas.className === "v86gl-d3d9-swapchain-overlay" &&
                    canvas.parentElement)
                canvas.parentElement.removeChild(canvas);
            else this.styleOverlayCanvas(canvas, 0, 0, 0, 0, false);
        }

        warnOnSharedD3DCanvasConflict(active) {
            if (active === "d3d8") this.d3d8BatchStreamSeen = true;
            if (active === "d3d9") this.d3d9BatchStreamSeen = true;
            if (!this.d3d8BatchStreamSeen || !this.d3d9BatchStreamSeen ||
                    this.d3d8Canvas !== this.d3d9Canvas ||
                    this.sharedD3DCanvasConflictReported) return;
            this.sharedD3DCanvasConflictReported = true;
            console.error("[v86gl] D3D8 and D3D9 streams cannot concurrently " +
                "own the same WebGPU canvas", { drivingThisBatch: active });
        }
    }

    global.V86GL_BRIDGE_VERSION = V86GL_BRIDGE_VERSION;
    global.installV86GLNetworkBridge = function(emulator, canvas, options) {
        return new V86GLNetworkBridge(emulator, canvas, options);
    };
})(typeof window !== "undefined" ? window : globalThis);
