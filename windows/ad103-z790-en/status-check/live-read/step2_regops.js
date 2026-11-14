'use strict';
// Step 2: harvest live RM handles from the target process, then issue ONE
// EXEC_REG_OPS READ_32 of a caller-chosen offset through D3DKMTEscape, reusing
// the recovered NVIDIA escape framing. Reads only. No register is written.

const gdi32 = Process.findModuleByName('gdi32.dll') || Process.findModuleByName('gdi32full.dll');
if (!gdi32) throw new Error('gdi32.dll not found');
const escExportPtr = gdi32.findExportByName('D3DKMTEscape');
if (!escExportPtr) throw new Error('D3DKMTEscape export not found');
const D3DKMTEscape = new NativeFunction(escExportPtr, 'uint32', ['pointer']);

const MAGIC = 0x4e564441, VER = 0x00010002, TAG = 0x4e562a2a, SUB_RMCTRL = 0x0500002b;
const CMD_EXEC_REG_OPS = 0x20800122;

let live = null;   // harvested handles

Interceptor.attach(escExportPtr, {
  onEnter(args) {
    if (live) return;
    const p = args[0];
    if (p.isNull()) return;
    let hAdapter, hDevice, type, pData, size, hContext;
    try {
      hAdapter = p.add(0).readU32();
      hDevice  = p.add(4).readU32();
      type     = p.add(8).readU32();
      pData    = p.add(16).readPointer();
      size     = p.add(24).readU32();
      hContext = p.add(28).readU32();
    } catch (e) { return; }
    if (type !== 0 || pData.isNull() || size < 0x44) return;
    let magic, sub, hClient, hObject, cmd;
    try {
      magic = pData.readU32();
      sub   = pData.add(0x10).readU32();
      hClient = pData.add(0x30).readU32();
      hObject = pData.add(0x34).readU32();
      cmd   = pData.add(0x38).readU32();
    } catch (e) { return; }
    if (magic !== MAGIC || sub !== SUB_RMCTRL) return;
    if ((cmd >>> 16) !== 0x2080) return;   // need the subdevice handle
    live = { hAdapter, hDevice, hContext, hClient, hObject, sampleCmd: cmd };
    send({ harvested: {
      hAdapter: '0x' + hAdapter.toString(16), hDevice: '0x' + hDevice.toString(16),
      hContext: '0x' + hContext.toString(16), hClient: '0x' + hClient.toString(16),
      hObject:  '0x' + hObject.toString(16),  sampleCmd: '0x' + cmd.toString(16) } });
  }
});

function readReg32(offset) {
  if (!live) throw new Error('handles not harvested yet');

  // --- NV2080_CTRL_GPU_REG_OP (32 bytes) ---
  const regop = Memory.alloc(32);
  regop.writeByteArray(new Array(32).fill(0));
  regop.add(0x00).writeU8(0x00);        // regOp = READ_32
  regop.add(0x01).writeU8(0x00);        // regType = GLOBAL
  regop.add(0x0c).writeU32(offset);     // regOffset

  // --- NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS (48 bytes) ---
  const params = Memory.alloc(0x30);
  params.writeByteArray(new Array(0x30).fill(0));
  params.add(0x14).writeU32(1);         // regOpCount = 1
  params.add(0x18).writePointer(regop); // regOps (NvP64) -> regop array
  // grRouteInfo (flags@0x20, route@0x28) left zero = ROUTE_TYPE_NONE

  // --- NVIDIA escape wrapper (68-byte header + params) ---
  const total = 0x44 + 0x30;
  const buf = Memory.alloc(total);
  buf.writeByteArray(new Array(total).fill(0));
  buf.add(0x00).writeU32(MAGIC);
  buf.add(0x04).writeU32(VER);
  buf.add(0x08).writeU32(total);
  buf.add(0x0c).writeU32(TAG);
  buf.add(0x10).writeU32(SUB_RMCTRL);
  buf.add(0x30).writeU32(live.hClient);
  buf.add(0x34).writeU32(live.hObject);
  buf.add(0x38).writeU32(CMD_EXEC_REG_OPS);
  buf.add(0x3c).writeU32(0x30);         // paramSize
  Memory.copy(buf.add(0x44), params, 0x30);
  const pParamsInBuf = buf.add(0x44);
  // the regOps pointer inside the copied params still points at `regop` (valid VA)

  // --- D3DKMT_ESCAPE (32 bytes) ---
  const esc = Memory.alloc(0x20);
  esc.writeByteArray(new Array(0x20).fill(0));
  esc.add(0x00).writeU32(live.hAdapter);
  esc.add(0x04).writeU32(live.hDevice);
  esc.add(0x08).writeU32(0);            // Type = DRIVERPRIVATE
  esc.add(0x0c).writeU32(0);            // Flags
  esc.add(0x10).writePointer(buf);
  esc.add(0x18).writeU32(total);
  esc.add(0x1c).writeU32(live.hContext);

  const nt = D3DKMTEscape(esc);

  const wrapperStatus = buf.add(0x40).readU32();
  const regStatus = pParamsInBuf.isNull() ? -1 : regop.add(0x02).readU8();
  const regValueLo = regop.add(0x14).readU32();
  const regValueHi = regop.add(0x10).readU32();
  return {
    offset: '0x' + offset.toString(16),
    ntStatus: '0x' + (nt >>> 0).toString(16),
    wrapperStatus: '0x' + wrapperStatus.toString(16),
    regStatus: '0x' + (regStatus & 0xff).toString(16),
    regValueLo: '0x' + (regValueLo >>> 0).toString(16),
    regValueHi: '0x' + (regValueHi >>> 0).toString(16)
  };
}

rpc.exports = {
  ready() { return live !== null; },
  read(offset) { return readReg32(offset >>> 0); }
};
