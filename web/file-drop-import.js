/**
 * StepMania Browser - File Drop Import (no companion needed)
 * Drag & drop an OGG/MP3 file onto the page to import it directly.
 * BPM detection runs in-browser via Web Audio API.
 */
(function() {
    const SONGS_BASE = '/Songs/YouTube';

    function detectBPM(audioBuffer) {
        const data = audioBuffer.getChannelData(0);
        const sr = audioBuffer.sampleRate;
        const step = Math.floor(sr / 200);
        const envelope = [];
        for (let i = 0; i < data.length; i += step) {
            let sum = 0;
            const end = Math.min(i + step, data.length);
            for (let j = i; j < end; j++) sum += data[j] * data[j];
            envelope.push(Math.sqrt(sum / (end - i)));
        }
        const max = Math.max(...envelope);
        const peaks = [];
        for (let i = 1; i < envelope.length - 1; i++) {
            if (envelope[i] > max * 0.3 && envelope[i] > envelope[i-1] && envelope[i] > envelope[i+1])
                peaks.push(i);
        }
        if (peaks.length < 2) return 120;
        const intervals = [];
        for (let i = 1; i < Math.min(peaks.length, 100); i++) intervals.push(peaks[i] - peaks[i-1]);
        intervals.sort((a, b) => a - b);
        const median = intervals[Math.floor(intervals.length / 2)];
        let bpm = 60 / ((median * step) / sr);
        while (bpm > 200) bpm /= 2;
        while (bpm < 80) bpm *= 2;
        return Math.round(bpm * 10) / 10;
    }

    window.smGenerateSM = function(title, artist, musicFile, bpm, duration, difficulty) {
        const ratings = { Beginner: 1, Easy: 3, Medium: 6, Hard: 8, Expert: 10 };
        const patterns = {
            Beginner: ["0100\n0000\n0000\n0000","0000\n0010\n0000\n0000","0000\n0000\n0001\n0000","0000\n0000\n0000\n1000"],
            Easy: ["0100\n0000\n0010\n0000","0001\n0000\n1000\n0000","0010\n0000\n0100\n0000","1000\n0000\n0001\n0000"],
            Medium: ["0100\n0010\n0001\n1000","1000\n0001\n0010\n0100","0010\n0100\n1000\n0001","0001\n1000\n0100\n0010"],
            Hard: ["1100\n0011\n1001\n0110","0110\n1001\n0011\n1100","1010\n0101\n1010\n0101","0101\n1010\n0101\n1010"],
            Expert: ["1100\n0110\n1001\n0110\n1100\n0011\n1001\n0011","0011\n1100\n0110\n1001\n0011\n1100\n0110\n1001","1010\n0101\n1010\n0101\n1100\n0011\n1100\n0011","1001\n0110\n1001\n0110\n1010\n0101\n1010\n0101"]
        };
        const pat = patterns[difficulty] || patterns.Medium;
        const measures = Math.ceil((duration / 60) * bpm / 4) + 1;
        const chart = Array.from({length: measures}, (_, i) => pat[i % pat.length]).join(',\n') + '\n';
        return `#TITLE:${title};\n#SUBTITLE:;\n#ARTIST:${artist};\n#TITLETRANSLIT:;\n#SUBTITLETRANSLIT:;\n#ARTISTTRANSLIT:;\n#GENRE:;\n#CREDIT:Browser Import;\n#BANNER:;\n#BACKGROUND:;\n#LYRICSPATH:;\n#CDTITLE:;\n#MUSIC:${musicFile};\n#OFFSET:0.000;\n#SAMPLESTART:30.000;\n#SAMPLELENGTH:12.000;\n#SELECTABLE:YES;\n#BPMS:0.000=${bpm.toFixed(3)};\n#STOPS:;\n#BGCHANGES:;\n#KEYSOUNDS:;\n\n#NOTES:\n     dance-single:\n     :\n     ${difficulty}:\n     ${ratings[difficulty] || 6}:\n     0.100,0.100,0.100,0.100,0.100:\n${chart};\n`;
    };

    window.smInjectSong = function(dirName, musicFile, audioBytes, smText) {
        if (!window.Module || !window.Module.FS) return false;
        const FS = window.Module.FS;
        const songPath = `${SONGS_BASE}/${dirName}`;
        try { FS.mkdirTree(songPath); } catch(e) {}
        FS.writeFile(`${songPath}/${musicFile}`, new Uint8Array(audioBytes));
        FS.writeFile(`${songPath}/song.sm`, smText);
        // Persist to IndexedDB
        FS.syncfs(false, err => { if (err) console.error('[IDBFS] sync error:', err); });
        // Auto-reload song list
        try { window.Module.ccall('sm_reload_songs', 'number', [], []); } catch(e) {}
        return true;
    };

    async function handleFileDrop(file) {
        const statusEl = document.getElementById('import-status');
        const setStatus = (msg, err) => {
            if (statusEl) { statusEl.textContent = msg; statusEl.className = 'import-status ' + (err ? 'error' : 'ok'); }
        };

        const name = file.name.replace(/\.(ogg|mp3|wav)$/i, '');
        let title = name, artist = 'Unknown';
        if (name.includes(' - ')) { [artist, title] = name.split(' - ', 2); }

        setStatus('⏳ Analyzing audio...', false);
        const arrayBuf = await file.arrayBuffer();
        const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        let audioBuffer;
        try { audioBuffer = await audioCtx.decodeAudioData(arrayBuf.slice(0)); }
        catch(e) { setStatus(`❌ Cannot decode audio: ${e.message}`, true); return; }

        const bpm = detectBPM(audioBuffer);
        const duration = audioBuffer.duration;
        const difficulty = document.getElementById('import-difficulty')?.value || 'Medium';
        const ext = file.name.split('.').pop().toLowerCase();
        const musicFile = `song.${ext}`;
        const dirName = `${artist} - ${title}`.replace(/[<>:"/\\|?*]/g, '_');

        const sm = window.smGenerateSM(title, artist, musicFile, bpm, duration, difficulty);
        if (!window.smInjectSong(dirName, musicFile, arrayBuf, sm)) {
            setStatus('❌ Game not loaded yet', true); return;
        }

        setStatus(`✅ ${artist} - ${title} (${bpm} BPM) imported. Songs reloaded!`, false);
        audioCtx.close();
    }

    function setupDragDrop() {
        const body = document.body;
        ['dragenter','dragover'].forEach(e => body.addEventListener(e, ev => { ev.preventDefault(); }));
        body.addEventListener('drop', e => {
            e.preventDefault();
            const file = e.dataTransfer?.files?.[0];
            if (file && /\.(ogg|mp3|wav)$/i.test(file.name)) handleFileDrop(file);
        });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', setupDragDrop);
    else setupDragDrop();

    // Ensure Songs/YouTube dir exists once module loads
    (function waitFS() {
        if (window.Module && window.Module.FS) {
            try { window.Module.FS.mkdirTree(SONGS_BASE); } catch(e) {}
        } else setTimeout(waitFS, 1000);
    })();
})();
