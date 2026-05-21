/**
 * StepMania Browser - YouTube Song Importer
 * Connects to local companion server to download YouTube songs,
 * then injects them into the Emscripten virtual filesystem.
 */
(function() {
    const COMPANION_URL = 'http://localhost:8765';
    const SONGS_BASE = '/Songs/YouTube';

    let moduleInstance = null;

    // Wait for StepMania WASM module to be ready
    function waitForModule() {
        return new Promise(resolve => {
            const check = () => {
                if (window.Module && window.Module.FS) {
                    moduleInstance = window.Module;
                    resolve(moduleInstance);
                } else {
                    setTimeout(check, 500);
                }
            };
            check();
        });
    }

    // Check if companion server is running
    async function checkCompanion() {
        try {
            const r = await fetch(`${COMPANION_URL}/health`, { signal: AbortSignal.timeout(2000) });
            return r.ok;
        } catch { return false; }
    }

    // Import a YouTube song
    async function importSong(url, difficulty = 'Medium') {
        const statusEl = document.getElementById('import-status');
        const setStatus = (msg, isError) => {
            if (statusEl) {
                statusEl.textContent = msg;
                statusEl.className = 'import-status ' + (isError ? 'error' : 'ok');
            }
        };

        setStatus('Checking companion server...', false);
        if (!(await checkCompanion())) {
            setStatus('❌ Companion server not running. Start it with: python companion/server.py', true);
            return;
        }

        setStatus('⏳ Downloading & processing (this may take 30-60s)...', false);

        try {
            const resp = await fetch(`${COMPANION_URL}/api/import`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url, difficulty })
            });

            const data = await resp.json();
            if (!data.success) {
                setStatus(`❌ ${data.error}`, true);
                return;
            }

            // Decode base64 OGG
            const oggBytes = Uint8Array.from(atob(data.ogg_b64), c => c.charCodeAt(0));
            const smText = data.sm;

            // Inject into Emscripten FS
            await waitForModule();
            const FS = moduleInstance.FS;

            const dirName = `${data.artist} - ${data.title}`
                .replace(/[^\x20-\x7E]/g, '')
                .replace(/[<>:"/\\|?*]/g, '_')
                .replace(/\s+/g, ' ')
                .trim()
                .substring(0, 60) || 'imported_song';
            const songPath = `${SONGS_BASE}/${dirName}`;

            // Ensure parent directories exist
            console.log('[Import] FS available:', !!FS);
            console.log('[Import] /Songs exists:', (() => { try { FS.stat('/Songs'); return true; } catch(e) { return false; } })());
            console.log('[Import] /Songs readdir:', (() => { try { return FS.readdir('/Songs'); } catch(e) { return e.message; } })());
            try { FS.mkdir('/Songs'); } catch(e) { console.log('[Import] mkdir /Songs:', e.message); }
            try { FS.mkdir('/Songs/YouTube'); } catch(e) { console.log('[Import] mkdir /Songs/YouTube:', e.message); }
            try {
                const parts = dirName.split('/');
                let path = '/Songs/YouTube';
                for (const p of parts) {
                    if (p) { path += '/' + p; try { FS.mkdir(path); } catch(e) { console.log('[Import] mkdir', path, e.message); } }
                }
            } catch(e) {}
            console.log('[Import] songPath exists:', (() => { try { FS.stat(songPath); return true; } catch(e) { return false; } })());

            // Write files
            try {
                try { FS.unlink(`${songPath}/song.ogg`); } catch(e) {}
                try { FS.unlink(`${songPath}/song.sm`); } catch(e) {}
                console.log('[Import] Writing OGG, size:', oggBytes.length);
                FS.writeFile(`${songPath}/song.ogg`, oggBytes);
                console.log('[Import] Writing SM');
                FS.writeFile(`${songPath}/song.sm`, smText);
                // Write background image if available
                if (data.bg_b64) {
                    const bgBytes = Uint8Array.from(atob(data.bg_b64), c => c.charCodeAt(0));
                    FS.writeFile(`${songPath}/bg.png`, bgBytes);
                    console.log('[Import] Wrote bg.png, size:', bgBytes.length);
                }
                console.log('[Import] Write success');
            } catch(e) {
                console.error('[Import] writeFile failed:', e, 'errno:', e.errno, 'code:', e.code);
                // Fallback: try writing to /tmp
                try {
                    FS.mkdir('/tmp/song'); 
                } catch(e2) {}
                try {
                    FS.writeFile('/tmp/song/song.ogg', oggBytes);
                    FS.writeFile('/tmp/song/song.sm', smText);
                    setStatus(`⚠️ Wrote to /tmp/song (FS issue with ${songPath}): ${e.message}`, true);
                    return;
                } catch(e3) {
                    setStatus(`❌ FS completely broken: ${e.message}. Is the game loaded?`, true);
                    return;
                }
            }

            // Persist to IndexedDB (if IDBFS mounted)
            if (FS.syncfs) FS.syncfs(false, err => { if (err) console.error('[IDBFS] sync error:', err); });

            // Auto-reload song list
            try { moduleInstance.ccall('sm_reload_songs', 'number', [], []); } catch(e) {}

            setStatus(`✅ Imported: ${data.artist} - ${data.title} (${data.bpm} BPM). Songs reloaded!`, false);

            // Add to imported songs list
            addToSongList(data);

        } catch (e) {
            setStatus(`❌ Error: ${e.message}`, true);
        }
    }

    function addToSongList(data) {
        const list = document.getElementById('imported-songs-list');
        if (!list) return;
        const li = document.createElement('li');
        li.textContent = `${data.artist} - ${data.title} [${data.difficulty}] (${data.bpm} BPM)`;
        list.appendChild(li);
    }

    // Create the import UI
    function createImportUI() {
        const overlay = document.getElementById('controls-overlay');
        if (!overlay) return;

        const panel = document.createElement('div');
        panel.id = 'youtube-import-panel';
        panel.innerHTML = `
            <div class="import-panel">
                <h3>🎵 YouTube Import</h3>
                <div class="import-row">
                    <input type="text" id="youtube-url" placeholder="Paste YouTube URL..." />
                    <select id="import-difficulty">
                        <option value="Beginner">Beginner</option>
                        <option value="Easy">Easy</option>
                        <option value="Medium" selected>Medium</option>
                        <option value="Hard">Hard</option>
                        <option value="Expert">Expert</option>
                    </select>
                    <button id="import-btn">Import</button>
                </div>
                <p id="import-status" class="import-status"></p>
                <details>
                    <summary>Imported Songs</summary>
                    <ul id="imported-songs-list"></ul>
                </details>
            </div>
        `;
        overlay.appendChild(panel);

        document.getElementById('import-btn').addEventListener('click', () => {
            const url = document.getElementById('youtube-url').value.trim();
            const diff = document.getElementById('import-difficulty').value;
            if (url) importSong(url, diff);
        });

        // Also support Enter key
        document.getElementById('youtube-url').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') document.getElementById('import-btn').click();
        });
    }

    // Initialize when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', createImportUI);
    } else {
        createImportUI();
    }

    // Also ensure the Songs/YouTube directory exists once module loads
    waitForModule().then(mod => {
        try { mod.FS.mkdirTree(SONGS_BASE); } catch(e) {}
    });
})();
