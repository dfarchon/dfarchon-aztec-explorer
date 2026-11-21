/**
 * Remote Explorer (Aztec S2 / Poseidon2) — Use a remote CUDA explore server.
 *
 * Compatible with dfpunk-aztec / https://dfaztec-s2.dfarchon.xyz/
 * Server must implement Barretenberg Poseidon2 planet hashing:
 *   hash = Poseidon2([planetHashKey, toFr(x), toFr(y)])
 *   planet iff hash < LOCATION_ID_UB / planetRarity
 *
 * Usage: set explore server URL (e.g. http://localhost:8880/explore), pick pattern, Explore!
 */

import {
  html,
  render,
  useEffect,
  useState,
} from "https://unpkg.com/htm/preact/standalone.module.js";

const NEW_CHUNK = "DiscoveredNewChunk";
const RETRY_MS = 750;
const RETRY_MAX_MS = 8000;

function locationIdFromDecStr(s) {
  const bi = BigInt(s);
  let h = bi.toString(16);
  while (h.length < 64) h = "0" + h;
  return h;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Normalize user URL to a POST /explore endpoint. */
function normalizeExploreUrl(raw) {
  let url = (raw || "").trim();
  if (!url) return "";
  // Common typo: /exp instead of /explore
  url = url.replace(/\/exp\/?$/i, "/explore");
  url = url.replace(/\/+$/, "");
  if (!/\/explore$/i.test(url)) {
    url = url + "/explore";
  }
  return url;
}

/** Format H/s as G / M / K units (e.g. 5698782 → "5.70 M"). */
function formatHashRate(rate) {
  const n = Number(rate) || 0;
  if (n >= 1e9) return `${(n / 1e9).toFixed(2)} G`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(2)} M`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(2)} K`;
  return `${Math.floor(n)}`;
}

function clearMinerIcon(id) {
  if (typeof ui?.removeExtraMinerLocation === "function") {
    try {
      ui.removeExtraMinerLocation(id);
    } catch (_) {
      /* ignore */
    }
  }
}

function getPattern(coords, patternType, chunkSize) {
  const constructors =
    typeof df.getConstructors === "function" ? df.getConstructors() : {};
  const {
    SwissCheesePattern,
    SpiralPattern,
    TowardsCenterPattern,
    TowardsCenterPatternV2,
  } = constructors;
  if (patternType === "swiss" && SwissCheesePattern)
    return new SwissCheesePattern(coords, chunkSize);
  if (patternType === "towardsCenter" && TowardsCenterPattern)
    return new TowardsCenterPattern(coords, chunkSize);
  if (patternType === "towardsCenterV2" && TowardsCenterPatternV2)
    return new TowardsCenterPatternV2(coords, chunkSize);
  if (SpiralPattern) return new SpiralPattern(coords, chunkSize);
  return null;
}

/**
 * Mimics a Web Worker for MinerManager.
 * Retries forever on network/HTTP errors so a server restart does not
 * permanently stall MinerManager (which waits for onmessage).
 */
class RemoteWorker {
  constructor(url) {
    this.url = url;
    this._alive = true;
    this.onmessage = null;
    this.onerror = null;
  }

  async postMessage(msg) {
    let msgJson;
    try {
      msgJson = JSON.parse(msg);
    } catch (err) {
      console.error("RemoteWorker: bad message", err);
      return;
    }

    let attempt = 0;
    let delay = RETRY_MS;
    while (this._alive) {
      attempt += 1;
      try {
        const resp = await fetch(this.url, {
          method: "POST",
          body: JSON.stringify({
            chunkFootprint: msgJson.chunkFootprint,
            planetRarity: msgJson.planetRarity,
            planetHashKey: msgJson.planetHashKey,
          }),
          headers: { "Content-Type": "application/json" },
        });
        if (!resp.ok) {
          const text = await resp.text().catch(() => "");
          throw new Error(
            `Explore server HTTP ${resp.status}: ${text.slice(0, 200)}`
          );
        }
        const exploredChunk = await resp.json();
        if (!exploredChunk?.chunkFootprint) {
          throw new Error("Explore server returned invalid chunk JSON");
        }

        const chunkCenter = {
          x:
            exploredChunk.chunkFootprint.bottomLeft.x +
            exploredChunk.chunkFootprint.sideLength / 2,
          y:
            exploredChunk.chunkFootprint.bottomLeft.y +
            exploredChunk.chunkFootprint.sideLength / 2,
        };
        if (typeof df.spaceTypePerlin === "function")
          exploredChunk.perlin = df.spaceTypePerlin(chunkCenter, false);

        const locs = exploredChunk.planetLocations || [];
        for (const planetLoc of locs) {
          if (planetLoc.hash != null)
            planetLoc.hash = locationIdFromDecStr(String(planetLoc.hash));
          if (typeof df.spaceTypePerlin === "function" && planetLoc.coords)
            planetLoc.perlin = df.spaceTypePerlin(planetLoc.coords, true);
          if (typeof df.biomebasePerlin === "function" && planetLoc.coords)
            planetLoc.biomebase = df.biomebasePerlin(planetLoc.coords, true);
        }

        if (this._alive && this.onmessage) {
          this.onmessage({
            data: JSON.stringify([exploredChunk, msgJson.jobId]),
          });
        }
        return;
      } catch (err) {
        console.warn(
          `RemoteWorker attempt ${attempt} failed (${this.url}):`,
          err?.message || err
        );
        if (this.onerror) this.onerror(err);
        if (!this._alive) return;
        await sleep(delay);
        delay = Math.min(delay * 1.5, RETRY_MAX_MS);
      }
    }
  }

  terminate() {
    this._alive = false;
  }
}

const inputStyle = {
  flex: 1,
  padding: "6px 8px",
  outline: "none",
  color: "#e4e4e4",
  background: "#3d444c",
  border: "1px solid #5a6268",
  borderRadius: "4px",
};
const btnStyle = {
  marginLeft: "8px",
  padding: "6px 12px",
  background: "#3d444c",
  color: "#e4e4e4",
  border: "1px solid #5a6268",
  borderRadius: "4px",
  cursor: "pointer",
};
const selectStyle = {
  padding: "6px 8px",
  background: "#3d444c",
  color: "#e4e4e4",
  border: "1px solid #5a6268",
  borderRadius: "4px",
  marginLeft: "8px",
};

function TargetIcon() {
  return html`
    <span
      style="display:inline-block;width:1em;height:1em;vertical-align:text-bottom;"
    >
      <svg width="100%" height="100%" viewBox="0 0 512 512" fill="white">
        <path
          d="M512 224h-50.462c-13.82-89.12-84.418-159.718-173.538-173.538v-50.462h-64v50.462c-89.12 13.82-159.718 84.418-173.538 173.538h-50.462v64h50.462c13.82 89.12 84.418 159.718 173.538 173.538v50.462h64v-50.462c89.12-13.82 159.718-84.418 173.538-173.538h50.462v-64zM396.411 224h-49.881c-9.642-27.275-31.255-48.889-58.53-58.53v-49.881c53.757 12.245 96.166 54.655 108.411 108.411zM256 288c-17.673 0-32-14.327-32-32s14.327-32 32-32c17.673 0 32 14.327 32 32s-14.327 32-32 32zM224 115.589v49.881c-27.275 9.641-48.889 31.255-58.53 58.53h-49.881c12.245-53.756 54.655-96.166 108.411-108.411zM115.589 288h49.881c9.641 27.275 31.255 48.889 58.53 58.53v49.881c-53.756-12.245-96.166-54.654-108.411-108.411zM288 396.411v-49.881c27.275-9.642 48.889-31.255 58.53-58.53h49.881c-12.245 53.757-54.654 96.166-108.411 108.411z"
        />
      </svg>
    </span>
  `;
}

function MinerUI({ miner, onRemove }) {
  const [hashRate, setHashRate] = useState(0);

  useEffect(() => {
    if (!miner || typeof miner.on !== "function") return;
    const onChunk = (chunk, miningTimeMillis) => {
      if (typeof df.addNewChunk === "function") df.addNewChunk(chunk);
      const rate =
        chunk?.chunkFootprint?.sideLength != null && miningTimeMillis > 0
          ? Math.floor(
              chunk.chunkFootprint.sideLength ** 2 / (miningTimeMillis / 1000)
            )
          : 0;
      setHashRate(rate);
      const res =
        typeof miner.getCurrentlyExploringChunk === "function"
          ? miner.getCurrentlyExploringChunk()
          : null;
      if (
        res?.bottomLeft != null &&
        res?.sideLength != null &&
        typeof ui?.setExtraMinerLocation === "function"
      ) {
        ui.setExtraMinerLocation(miner.id, {
          x: res.bottomLeft.x + res.sideLength / 2,
          y: res.bottomLeft.y + res.sideLength / 2,
        });
      } else {
        clearMinerIcon(miner.id);
      }
    };
    miner.on(NEW_CHUNK, onChunk);
    return () => {
      miner.off(NEW_CHUNK, onChunk);
      clearMinerIcon(miner.id);
    };
  }, [miner]);

  const [targeting, setTargeting] = useState(false);

  useEffect(() => {
    if (!targeting || !miner) return;
    const hover = () => {
      const coords = ui?.getHoveringOverCoords?.();
      if (coords && typeof ui?.setExtraMinerLocation === "function")
        ui.setExtraMinerLocation(miner.id, coords);
    };
    const click = () => {
      window.removeEventListener("mousemove", hover);
      window.removeEventListener("click", click);
      const coords = ui?.getHoveringOverCoords?.();
      if (coords) {
        const pattern = getPattern(coords, miner.patternType, miner.chunkSize);
        if (pattern && typeof miner.setMiningPattern === "function")
          miner.setMiningPattern(pattern);
      }
      if (typeof miner.startExplore === "function") miner.startExplore();
      setTargeting(false);
    };
    if (typeof miner.stopExplore === "function") miner.stopExplore();
    window.addEventListener("mousemove", hover);
    window.addEventListener("click", click);
    return () => {
      window.removeEventListener("mousemove", hover);
      window.removeEventListener("click", click);
      clearMinerIcon(miner.id);
    };
  }, [targeting, miner]);

  return html`
    <div
      style="padding-bottom:10px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:6px;"
    >
      <span style="min-width:0; flex:1;"
        >${miner.url} – chunk ${miner.chunkSize} – ${formatHashRate(
          hashRate
        )} hashes/sec</span
      >
      <div style="display:flex; gap:6px;">
        <button style=${btnStyle} onClick=${() => setTargeting(true)}>
          <${TargetIcon} />
        </button>
        <button
          style=${{ ...btnStyle, background: "#6b2d2d" }}
          onClick=${() => onRemove(miner)}
        >
          X
        </button>
      </div>
    </div>
  `;
}

function App({ initialMiners = [], addMiner, removeMiner }) {
  const [miners, setMiners] = useState(initialMiners);
  const [nextUrl, setNextUrl] = useState("http://localhost:8880/explore");
  const [patternType, setPatternType] = useState("spiral");
  const [chunkSize, setChunkSize] = useState(512);
  const [status, setStatus] = useState("");

  const add = () => {
    const url = normalizeExploreUrl(nextUrl);
    if (!url) {
      setStatus("Enter an explore server URL");
      return;
    }
    setStatus("");
    const next = addMiner(url, patternType, Number(chunkSize));
    setMiners([...next]);
  };

  const remove = (miner) => {
    setMiners([...removeMiner(miner)]);
  };

  return html`
    <div>
      <div style="margin-bottom:8px;font-size:12px;opacity:0.85;">
        Aztec S2 Poseidon2 remote explorer — URL must end with /explore. Chunk
        size 256 / 512 / 1024 (larger = fewer round-trips, more GPU work).
      </div>
      ${miners.map(
        (miner) =>
          html`<${MinerUI}
            key=${miner.id}
            miner=${miner}
            onRemove=${remove}
          />`
      )}
      <div
        style="display:flex; align-items:center; flex-wrap:wrap; gap:8px; margin-top:12px;"
      >
        <input
          type="text"
          style=${inputStyle}
          value=${nextUrl}
          onInput=${(e) => setNextUrl(e.target?.value ?? "")}
          placeholder="http://localhost:8880/explore"
        />
        <select
          style=${selectStyle}
          value=${patternType}
          onChange=${(e) => setPatternType(e.target?.value)}
        >
          <option value="spiral">Spiral</option>
          <option value="swiss">Swiss</option>
          <option value="towardsCenter">TowardsCenter</option>
          <option value="towardsCenterV2">TowardsCenterV2</option>
        </select>
        <select
          style=${selectStyle}
          value=${chunkSize}
          onChange=${(e) => setChunkSize(Number(e.target?.value))}
          title="Chunk side length"
        >
          <option value=${256}>256</option>
          <option value=${512}>512</option>
          <option value=${1024}>1024</option>
        </select>
        <button style=${btnStyle} onClick=${add}>Explore!</button>
      </div>
      ${status
        ? html`<div style="margin-top:8px;color:#f88;font-size:12px;">
            ${status}
          </div>`
        : null}
    </div>
  `;
}

export default class Plugin {
  constructor() {
    this.miners = [];
    this.id = 0;
  }

  _destroyMiner = (miner) => {
    clearMinerIcon(miner.id);
    try {
      if (typeof miner.stopExplore === "function") miner.stopExplore();
    } catch (_) {
      /* ignore */
    }
    try {
      if (typeof miner.destroy === "function") miner.destroy();
    } catch (_) {
      /* ignore */
    }
  };

  addMiner = (url, patternType = "spiral", chunkSize = 512) => {
    const allowed = new Set([256, 512, 1024]);
    chunkSize = Number(chunkSize);
    if (!allowed.has(chunkSize)) chunkSize = 512;

    // Replace existing miner on same URL so Explore! recovers after server restart
    const existing = this.miners.filter((m) => m.url === url);
    for (const m of existing) this._destroyMiner(m);
    this.miners = this.miners.filter((m) => m.url !== url);

    const constructors =
      typeof df.getConstructors === "function" ? df.getConstructors() : {};
    const Miner = constructors.MinerManager;
    if (!Miner || typeof Miner.create !== "function") {
      console.warn(
        "Remote Explorer: getConstructors().MinerManager not available"
      );
      return this.miners;
    }
    const chunkStore =
      typeof df.getChunkStore === "function" ? df.getChunkStore() : null;
    const worldRadius =
      typeof df.getWorldRadius === "function" ? df.getWorldRadius() : 0;
    const planetRarity = df?.planetRarity ?? 16;
    const hashConfig =
      typeof df.getHashConfig === "function" ? df.getHashConfig() : null;
    if (!chunkStore || !hashConfig) {
      console.warn(
        "Remote Explorer: getChunkStore or getHashConfig not available"
      );
      return this.miners;
    }
    // Ensure MinerManager also gets rarity on the wire (hashConfig.planetRarity)
    const hc = { ...hashConfig, planetRarity };
    const pattern = getPattern({ x: 0, y: 0 }, patternType, chunkSize);
    if (!pattern) {
      console.warn("Remote Explorer: pattern not available");
      return this.miners;
    }
    const miner = Miner.create(
      chunkStore,
      pattern,
      worldRadius,
      planetRarity,
      hc,
      false,
      () => new RemoteWorker(url)
    );
    miner.url = url;
    miner.id = this.id++;
    miner.chunkSize = chunkSize;
    miner.patternType = patternType;
    if (typeof miner.startExplore === "function") miner.startExplore();
    this.miners.push(miner);
    return this.miners;
  };

  removeMiner = (miner) => {
    this.miners = this.miners.filter((m) => {
      if (m !== miner) return true;
      this._destroyMiner(m);
      return false;
    });
    return this.miners;
  };

  async render(container) {
    container.style.minWidth = "450px";
    container.style.width = "auto";
    render(
      html`<${App}
        initialMiners=${this.miners}
        addMiner=${this.addMiner}
        removeMiner=${this.removeMiner}
      />`,
      container
    );
  }

  destroy() {
    for (const miner of this.miners) this._destroyMiner(miner);
    this.miners = [];
  }
}
