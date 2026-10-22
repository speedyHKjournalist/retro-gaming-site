globalThis.V86GL_BUILD_REVISION = "6a5936bfd189a5d6221e";
// The v86gl virtio device: the CPU side of the graphics proxy, a descriptor
// for v86's virtio_devices. It runs where the CPU runs (the page, or the CPU
// worker), knows nothing of WebGPU, and talks to the renderer
// (graphics_proxy.js) over a channel. See docs/graphics-proxy-plugin-plan.md.
//
// The PCI identity, slot, ports, configuration space and request layout are
// v86gl.sys's ABI. The VGL2/GLWG/D9WG rendering protocols ride in the batches
// unchanged. See docs/glbridge.md.
//
// Channel messages, device -> host:
//   batch      { id, generation, frameId, flags, commandCount, descAddr,
//                descLen, batchAddr, responseBase, submitCount, bytes }
// host -> device:
//   write      { generation, offset, bytes }  readback into the arena
//   done       { id }                         the batch no longer needs its capacity
//   available  { value }                      whether a renderer takes batches

(function(global) {
    "use strict";

    const MAGIC = 0x324C4756;
    // v86gl_pci.js's snapshots (state[92] before virtio_devices)
    const LEGACY_STATE_MAGIC = 0x56514731;
    const STATE_VERSION = 2;
    const ARENA_BYTES = 16 * 1024 * 1024;
    const RESPONSE_AREA_BYTES = 4 * 1024 * 1024;
    const HEADER_BYTES = 32;
    const REQUEST_BYTES = 24;
    const RESPONSE_BYTES = 16;
    const F_SHARED_ARENA = 0;
    const F_VERSION_1 = 32;
    const REGISTER_ARENA = 1;
    const SUBMIT = 2;
    const UNREGISTER_ARENA = 3;
    const OK = 0;
    const INVALID = 1;
    const NO_ARENA = 2;
    const NO_RENDERER = 3;

    // Flow control across threads only: on one thread the renderer takes each
    // batch as it comes, as it always has
    const MAX_INFLIGHT_BATCHES = 8;
    const MAX_INFLIGHT_BYTES = 32 * 1024 * 1024;

    /**
     * @param channel { remote, post(message, transfer), listen(handler) }
     * @return a virtio_devices descriptor
     */
    function createV86GLDevice(channel) {
        const remote = !!channel.remote;
        let dev = null;
        let arenaAddress = 0;
        let arenaBytes = 0;
        let arenaGeneration = 0;
        let lastFrameId = 0;
        let lastBytes = 0;
        let submitCount = 0;
        let hostAvailable = false;
        let nextBatch = 0;
        const inflight = new Map();
        let inflightBytes = 0;
        let processing = false;
        let processAgain = false;

        function releaseArena() {
            ++arenaGeneration;
            arenaAddress = 0;
            arenaBytes = 0;
        }

        function forgetInflight() {
            inflight.clear();
            inflightBytes = 0;
        }

        function canAccept() {
            return !remote || inflight.size < MAX_INFLIGHT_BATCHES &&
                inflightBytes <= MAX_INFLIGHT_BYTES - ARENA_BYTES;
        }

        channel.listen(message => {
            switch (message.type) {
                case "write": {
                    // A readback lands in the arena of the batch that asked for it
                    const offset = message.offset;
                    const bytes = message.bytes;
                    if (message.generation !== arenaGeneration || !arenaBytes) return;
                    if (!Number.isInteger(offset) || offset < 0 || offset + bytes.length > arenaBytes)
                        throw new RangeError("v86gl write outside registered arena");
                    dev.write_memory(bytes, arenaAddress + offset);
                    break;
                }
                case "done": {
                    const bytes = inflight.get(message.id);
                    if (bytes === undefined) return;
                    inflight.delete(message.id);
                    inflightBytes -= bytes;
                    // Descriptors left with the guest while the renderer was full
                    processQueue();
                    break;
                }
                case "available":
                    hostAvailable = !!message.value;
                    break;
            }
        });

        // A local renderer answers inside post(): its "done" comes back while
        // the queue is being processed
        function processQueue() {
            if (processing) {
                processAgain = true;
                return;
            }
            processing = true;
            try {
                do {
                    processAgain = false;
                    processRequests();
                } while (processAgain);
            } finally {
                processing = false;
            }
        }

        function processRequests() {
            if (!dev.is_feature_negotiated(F_SHARED_ARENA) || !dev.is_feature_negotiated(F_VERSION_1)) return;
            // Leave descriptors with the guest until the renderer has room
            while (canAccept() && dev.has_request(0)) {
                const request = dev.pop_request(0);
                if (!request) break;
                if (request.readable !== REQUEST_BYTES || request.writable !== RESPONSE_BYTES) {
                    dev.needs_reset();
                    break;
                }
                let result = INVALID;
                try {
                    result = handleRequest(request.read());
                } catch (error) {
                    console.error("[virtio-v86gl] request failed", error);
                }
                request.write(new Uint8Array(new Uint32Array([result, lastFrameId, lastBytes, submitCount]).buffer));
                request.complete();
            }
            dev.flush(0);
        }

        function handleRequest(request) {
            const view = new DataView(request.buffer, request.byteOffset, REQUEST_BYTES);
            const op = view.getUint32(0, true);
            const address = view.getUint32(4, true);
            const high = view.getUint32(8, true);
            const length = view.getUint32(12, true);
            const flags = view.getUint32(16, true);
            if (view.getUint32(20, true)) return INVALID;
            if (op === REGISTER_ARENA) {
                // A 64-bit guest-physical address: RAM anywhere on the 36-bit bus
                const arena = address + high * 0x100000000;
                if (arenaBytes || flags || length !== ARENA_BYTES || !arena || !dev.is_ram(arena, length))
                    return INVALID;
                ++arenaGeneration;
                arenaAddress = arena;
                arenaBytes = length;
                return OK;
            }
            if (address || high) return INVALID;
            if (op === UNREGISTER_ARENA) {
                if (length || flags) return INVALID;
                releaseArena();
                return OK;
            }
            if (op !== SUBMIT || (flags & ~1)) return INVALID;
            if (!arenaBytes) return NO_ARENA;
            if (length < HEADER_BYTES || length > arenaBytes) return INVALID;

            // A view of guest RAM while the batch has one contiguous backing
            // (checked per submission: the physical map may have changed)
            const raw = dev.read_memory(arenaAddress, length);
            const header = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
            const commandBytes = header.getUint32(20, true);
            // VGL2 reserved words belong to the graphics protocol, not to the
            // virtqueue request. Shipping D3D8/D3D9/DDraw DLLs put D9WG_MAGIC in
            // reserved0; the old PCI transport ignored both words. Preserve that ABI.
            if (header.getUint32(0, true) !== MAGIC || header.getUint32(4, true) !== 1 ||
                    commandBytes !== length - HEADER_BYTES || (header.getUint32(8, true) & ~1))
                return INVALID;
            if (!hostAvailable) return NO_RENDERER;

            const id = ++nextBatch;
            // Across threads a copy the renderer owns; never the guest's memory
            const bytes = remote ? raw.slice(HEADER_BYTES) : raw.subarray(HEADER_BYTES);
            inflight.set(id, bytes.byteLength);
            inflightBytes += bytes.byteLength;
            const frameId = header.getUint32(12, true);
            channel.post({
                type: "batch",
                id,
                generation: arenaGeneration,
                frameId,
                flags: header.getUint32(8, true) | flags,
                commandCount: header.getUint32(16, true),
                descAddr: arenaAddress,
                descLen: length,
                batchAddr: arenaAddress + HEADER_BYTES,
                responseBase: arenaBytes - RESPONSE_AREA_BYTES - HEADER_BYTES,
                submitCount: submitCount + 1,
                bytes,
            }, remote ? [bytes.buffer] : undefined);
            lastFrameId = frameId;
            lastBytes = commandBytes;
            ++submitCount;
            return OK;
        }

        return {
            name: "v86gl",
            // The slot keeps Windows' device instance, and so the installed driver
            pci_slot: 0x13,
            io_base: 0xF100,
            // Provisional, unallocated local ID. NOT the standard virtio-gpu ID.
            device_id: 0x107F,
            subsystem_device_id: 0x5686,
            features: [F_SHARED_ARENA],
            queues: [{ size: 8 }],
            config: [
                { bytes: 4, name: "magic", read: () => MAGIC },
                { bytes: 4, name: "version", read: () => 1 },
                { bytes: 4, name: "arena_bytes", read: () => ARENA_BYTES },
                { bytes: 4, name: "max_batch_bytes", read: () => ARENA_BYTES },
            ],
            init(handle) {
                dev = handle;
            },
            notify() {
                processQueue();
            },
            // Batches the renderer has not acknowledged yet
            busy() {
                return inflight.size;
            },
            reset() {
                releaseArena();
                lastFrameId = lastBytes = submitCount = 0;
                forgetInflight();
            },
            get_state() {
                return [STATE_VERSION, arenaAddress, arenaBytes, lastFrameId, lastBytes, submitCount];
            },
            set_state(state) {
                if (!Array.isArray(state) || state[0] !== STATE_VERSION)
                    throw new Error("Unsupported virtio-v86gl state in snapshot");
                const [, address, bytes, frameId, last, count] = state;
                if (bytes && (bytes !== ARENA_BYTES || !dev.is_ram(address, bytes)))
                    throw new Error("Invalid virtio-v86gl arena in snapshot");
                releaseArena();
                arenaAddress = address;
                arenaBytes = bytes;
                lastFrameId = frameId;
                lastBytes = last;
                submitCount = count;
                forgetInflight();
            },
            // [magic, transport, arena address, arena bytes, frame id, bytes,
            //  submit count, 0, graphics checkpoint]
            upgrade_state(slot) {
                if (!Array.isArray(slot) || slot[0] !== LEGACY_STATE_MAGIC) return null;
                return [slot[1], [STATE_VERSION, slot[2], slot[3], slot[4], slot[5], slot[6]], slot[8]];
            },
        };
    }

    const factories = global.V86VirtioDeviceFactories || (global.V86VirtioDeviceFactories = {});
    factories.v86gl = createV86GLDevice;
    if (typeof module !== "undefined" && module.exports)
        module.exports = { createV86GLDevice };
})(typeof globalThis !== "undefined" ? globalThis : this);
