// Observe-only: log D3DKMTEscape private-driver-data buffers from nvidia-smi.
// Reads the caller's own escape struct; changes nothing.
'use strict';

const MAXDUMP = 512;         // cap bytes dumped per call
const seen = {};             // dedupe identical (type,size,first16) signatures
let count = 0;

function hookEscape(addr, label) {
  Interceptor.attach(addr, {
    onEnter(args) {
      // D3DKMT_ESCAPE* in args[0]. Layout (x64):
      //  +0 hAdapter u32, +4 hDevice u32, +8 Type u32, +12 Flags u32,
      //  +16 pPrivateDriverData ptr, +24 PrivateDriverDataSize u32, +28 hContext u32
      const p = args[0];
      if (p.isNull()) return;
      let hAdapter, type, pData, size;
      try {
        hAdapter = p.add(0).readU32();
        type     = p.add(8).readU32();
        pData    = p.add(16).readPointer();
        size     = p.add(24).readU32();
      } catch (e) { return; }
      if (pData.isNull() || size === 0 || size > (1 << 20)) return;
      const n = Math.min(size, MAXDUMP);
      let bytes;
      try { bytes = pData.readByteArray(n); } catch (e) { return; }
      const u8 = new Uint8Array(bytes);
      const first16 = Array.from(u8.slice(0, 16)).map(x => ('0'+x.toString(16)).slice(-2)).join('');
      const sig = label + ':' + type + ':' + size + ':' + first16;
      if (seen[sig]) { seen[sig]++; return; }
      seen[sig] = 1;
      count++;
      send({ label, escapeType: type, hAdapter: '0x'+hAdapter.toString(16),
             privateSize: size, dumped: n, first16 }, bytes);
    }
  });
}

let hooked = 0;
for (const modName of ['gdi32.dll', 'gdi32full.dll', 'win32u.dll']) {
  const m = Process.findModuleByName(modName);
  if (!m) continue;
  for (const expName of ['D3DKMTEscape', 'NtGdiDdDDIEscape']) {
    const ex = m.findExportByName(expName);
    if (ex) { hookEscape(ex, modName + '!' + expName); hooked++; }
  }
}
send({ note: 'hooks installed', hooked });
