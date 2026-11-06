// Read-only diagnostic: existing Wasm binary, debugger locals, optional main source substitution in memory.
const fs = require('fs');
const path = require('path');
const inspector = require('inspector');
const Module = require('module');
const {execFileSync} = require('child_process');
const file = path.resolve('.chibi/integration/wasm/test_language.js');
const baseline = process.argv[2] === 'main-source';
const source = path.resolve('tools/compiler/package_compiler/Compile.chibi');
const current = fs.readFileSync(source);
const main = baseline ? execFileSync('git', ['show', '7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62:tools/compiler/package_compiler/Compile.chibi']) : null;
const session = new inspector.Session();
session.connect();
session.post('Debugger.enable');
let breakpointSet = false;
session.on('Debugger.scriptParsed', ({params: p}) => {
    if (breakpointSet || !p.url.startsWith('wasm://')) return;
    breakpointSet = true;
    session.post('Debugger.setBreakpoint', {location: {scriptId: p.scriptId, lineNumber: 0, columnNumber: 2421656}});
});
session.on('Debugger.paused', ({params: p}) => {
    const frame = p.callFrames[0];
    const scope = frame.scopeChain.find(s => s.type === 'local');
    session.post('Runtime.getProperties', {objectId: scope.object.objectId, ownProperties: true}, (err, r) => {
        if (err) throw err;
        const item = r.result.find(v => v.name === '$var0');
        session.post('Runtime.getProperties', {objectId: item.value.objectId, ownProperties: true}, (err, v) => {
            if (err) throw err;
            const pointer = v.result.find(x => x.name === 'value').value.value;
            const heap = globalThis.diagHeap();
            const view = new DataView(heap.buffer);
            const u = a => view.getUint32(a, true), i = a => view.getInt32(a, true);
            const len = a => a ? i(a - 32) : 0;
            const str = a => a ? Buffer.from(heap.subarray(a, heap.indexOf(0, a))).toString() : null;
            const code = u(pointer + 272), slots = u(pointer + 276), sources = u(pointer + 292), n = len(sources);
            const ranges = {};
            const sections = {prologue:0, phases:0, stepDispatch:0, stepApply:0, finalization:0};
            for (let j = 0; j < n; j++) {
                const a = sources + j * 40;
                const asset = str(u(a + 20));
                if (!asset) continue;
                const row = ranges[asset] ||= {count: 0, firstLine: Infinity, lastLine: 0, firstStart: Infinity, lastEnd: 0};
                row.count++;
                const line = i(a + 32);
                sections[line < 2641 ? "prologue" : line < 3280 ? "phases" : line < 3426 ? "stepDispatch" : line < 3608 ? "stepApply" : "finalization"]++;
                row.firstLine = Math.min(row.firstLine, i(a + 32));
                row.lastLine = Math.max(row.lastLine, i(a + 32));
                row.firstStart = Math.min(row.firstStart, i(a + 24));
                row.lastEnd = Math.max(row.lastEnd, i(a + 28));
            }
            console.error('EMISSION_DIAGNOSTIC ' + JSON.stringify({baseline, function: frame.functionName, instructions: len(code), slots: len(slots), sourceRecords: n, compactInstructions: len(u(pointer + 320)), ranges, sections}));
            session.post('Debugger.resume');
        });
    });
});
globalThis.diagSubstitute = heap => {
    if (!baseline) return;
    if (main.length > current.length) throw Error('Baseline does not fit existing source storage');
    const buffer = Buffer.from(heap.buffer);
    let at = -1, count = 0;
    while ((at = buffer.indexOf(current, at + 1)) >= 0) {
        buffer.fill(32, at, at + current.length);
        main.copy(buffer, at);
        count++;
    }
    if (!count) throw Error('Embedded current compiler source not found');
    console.error('SOURCE_SUBSTITUTION ' + JSON.stringify({count, mainBytes: main.length, currentBytes: current.length, padding: current.length - main.length}));
};
process.argv = [process.execPath, file, '--package-compiler-chibi-compiled'];
const m = new Module(file, module);
m.filename = file;
m.paths = Module._nodeModulePaths(path.dirname(file));
m._compile(fs.readFileSync(file, 'utf8') + '\n globalThis.diagHeap = () => HEAPU8; Module.onRuntimeInitialized = () => globalThis.diagSubstitute(HEAPU8);\n', file);
