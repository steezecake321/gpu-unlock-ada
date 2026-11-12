/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ASUS ROG STRIX Z790-A GAMING WIFI D4 / RTX 3090 Ti (GA102) ownership
 * adapter.
 *
 * Adapted from windows/ad103-z790-en-REFERENCE/z790_release.c. All
 * structural logic (Snapshot/SameEdge/Removed/ProtectedHandle/ProtectedEdge/
 * OwnEdge/KnownEdge/UsbPointerPath/PointerOwner/PointerAddition/
 * PointerBaseline/UsbConsoleChild/UsbBaseline/UsbRemoved/Expected/NoteEdge/
 * GraphDiff/ReportAdditions/DecodeMask/ClaimRestore/GuGa102Release/
 * GuGa102Released/GuGa102Finish/GuGa102Mutated) is unchanged except for the
 * substitutions below -- every one of them this board's own real capture
 * from probe/ (2026-11-10 and 2026-11-11, confirmed stable across two
 * separate genuine cold boots), never copied from the AD103 reference "by
 * analogy":
 *
 *   - This board's architecture is NOT a 3-stage out/gfx/nv structure. Two
 *     separate cold boots (GA102-DIAG-4.TXT, GA102-DIAG-5.TXT) found no
 *     SimpleTextOut consumer bound BY_DRIVER to the physical GOP at all --
 *     this board's console publishes SimpleTextOut directly, indirectly,
 *     through a second GOP-protocol handle (see below), not through a
 *     dedicated "out driver" on the physical GOP the way ad104-msi/AD103
 *     have. In its place, both captures found a previously-unidentified
 *     agent bound BY_DRIVER to EFI_EDID_DISCOVERED_PROTOCOL_GUID on the
 *     physical GOP. So this port has an edid_agent stage instead of an
 *     out_agent stage: stage 1 detaches edid_agent, stage 2 detaches
 *     gfx_agent, stage 3 detaches nv_agent and claims the GPU's PCI_IO
 *     exclusively -- two display-side stages, not three.
 *
 *   - PciPath/GopPath/UsbHostPath: this board's own ACPI/PCI device paths,
 *     captured and confirmed byte-identical across both runs. UsbHostPath
 *     happens to be byte-identical to the AD103 reference's own constant,
 *     confirmed programmatically against this board's own capture, not
 *     copied by eye -- both boards are Z790/PCH platforms and the PCH's
 *     built-in xHCI controller sits at the same ACPI/PCI location (device
 *     0x14) on both. GopPath/PciPath do not match AD103 -- different
 *     machine, different topology.
 *
 *   - NvFile: this board's own captured LoadedImage FilePath, NVIDIA's own
 *     GOP driver filename for Ampere ("nvgop-ga1xx", decoded from the raw
 *     UCS-2 bytes), embedded in the VBIOS Option ROM -- same general naming
 *     scheme as the AD103 reference's own Ada-generation "nvgop-ad1xx",
 *     different generation suffix, both NVIDIA's own convention, not
 *     board-specific. GfxFile/EdidFile: this board's own captured
 *     GUID-keyed FirmwareVolumeFile device-path nodes; unlike the AD103
 *     reference's OutFile/GfxFile, these do NOT match ad104-msi's own
 *     constants (different platform firmware, no reason to expect a
 *     match, and none found).
 *
 *   - Driver() pins (version/size/stop/code offsets, and every hash): this
 *     board's own captured driver identities. The hashed code range is
 *     whichever section this board's own loaded driver's PE headers mark
 *     as code, same reproducible-not-hand-picked discipline as the AD103
 *     reference. Unlike every Driver() check there, the edid driver here
 *     (which==0) loads as EfiRuntimeServicesCode (ImageCodeType==5), not
 *     EfiBootServicesCode (3) -- confirmed both runs, not a typo; nv/gfx
 *     are EfiBootServicesCode same as the reference.
 *
 *   - This board's physical GOP's own PCI-side child count is 1 (just the
 *     physical GOP itself), not the AD103 reference's own 1-or-2 PCI child
 *     situation -- no second, otherwise-unreferenced PCI-side child exists
 *     here. The complexity on this board is on the GOP side instead: the
 *     physical GOP's own GOP protocol has a SECOND consumer relationship,
 *     same agent as the primary BY_DRIVER gfx_agent edge, opened
 *     BY_CHILD_CONTROLLER naming a second GOP-protocol handle
 *     ("Plan.gop_child") that gfx_agent itself created. That second handle
 *     has no device path of its own (capture explicitly failed to walk
 *     one, both runs) and is not the console handle, but it is NOT inert
 *     either -- unlike the AD103 reference's own extra PCI child, which
 *     had zero further graph entries, this one has exactly three: a
 *     BY_DRIVER SimpleTextOut self-bind, a BY_CHILD_CONTROLLER SimpleTextOut
 *     relationship naming the console handle as child (this is, indirectly,
 *     how this board's console gets its SimpleTextOut at all -- not via any
 *     direct edge on the physical GOP), and a BY_DRIVER GOP self-bind by a
 *     second, otherwise-unrelated agent. All three, plus the gfx_agent
 *     child-controller edge that names this handle, are confirmed
 *     byte-for-byte identical in SHAPE (same protocols/attributes/
 *     relationships) across both captures -- only the raw handle pointer
 *     values differ between runs, exactly as Plan.gop's own pointer value
 *     already does, and for the same reason (firmware-allocated, not a
 *     fixed address). Gops()/Consumers() below require exactly this shape,
 *     nothing more or less, and fail closed on any deviation.
 *
 *     Plan.gop_child's own teardown at stage 2 was originally reasoned
 *     from the UEFI spec, before the first real hardware attempt:
 *     DisconnectController(Plan.gop,gfx_agent,NULL) with a NULL
 *     ChildHandleBuffer disconnects every child controller gfx_agent
 *     created on Plan.gop (i.e. Plan.gop_child) before disconnecting
 *     gfx_agent itself -- so the assumption was that Plan.gop_child's
 *     whole subtree (all three of its own edges) goes with it. Five real
 *     hardware runs (2026-11-11, RUN0006.TXT through RUN0011.TXT) showed
 *     that is wrong, one edge at a time, via GraphDiff()'s own logging:
 *     stage 2 itself always returns status=0 (the gfx_agent-on-Plan.gop
 *     disconnect this file actually asks for genuinely happens), but
 *     NONE of Plan.gop_child's three own edges -- Consumers()'s
 *     "selfText" (BY_DRIVER SimpleTextOut self-bind), "splitText"
 *     (BY_CHILD_CONTROLLER SimpleTextOut naming Plan.console as child,
 *     how this handle's SimpleTextOut reaches the console splitter at
 *     all), or "selfGop" (BY_DRIVER GOP self-bind by a second,
 *     otherwise-unrelated agent) -- are ever retracted. All three survive
 *     stage 2 on this board, intact and byte-identical in shape to their
 *     Original-capture edges (selfText/splitText share one agent,
 *     0x699c8018 both runs it was seen; selfGop's own agent, 0x699c4998,
 *     is identical across RUN0010.TXT and RUN0011.TXT despite
 *     Plan.gop/Plan.gop_child's own pointer values differing between
 *     those two boots -- a stable, pre-existing driver, not something
 *     spun up by this file's own disconnect calls). None of the three are
 *     created by gfx_agent's own binding on Plan.gop, so disconnecting
 *     gfx_agent from Plan.gop -- which is all this file actually calls --
 *     never touches them. The only edge that does disappear at stage 2 is
 *     gfx_agent's own BY_CHILD_CONTROLLER edge on Plan.gop naming
 *     Plan.gop_child -- already covered by the existing gfx_agent/GopGuid
 *     clause above, nothing Plan.gop_child-specific needed. Removed()/
 *     Expected()/GraphDiff() below no longer treat Plan.gop_child's own
 *     edges as removed or banned at stage>=2 at all -- confirmed to
 *     persist unchanged, they are now checked exactly like any other
 *     still-present edge in the graph, the same ordinary machinery
 *     everything else in this file uses.
 *
 *   - Bar()'s AddrRangeMax: GU_BAR0_BASE + 0xFFFFFF = 0x84FFFFFF, recomputed
 *     for this board's own BAR0_BASE (0x84000000), not reused from the
 *     AD103 reference's literal (which was for BAR0_BASE=0x81000000).
 *
 *   - GuGa102ReadyForWindows()'s chip id: 0xb72000a1 (GA102, this board's
 *     own profile.c Registers() offset-0 pin), not the AD103 reference's
 *     AD103 value 0x193000a1. The post-unlock SS0/SS1/PLM target values
 *     (0x88888888 / 0x8 / 0xFFFFFFFF) are NOT board-specific -- confirmed
 *     against docs/How Ampere was unlocked.md, which documents this exact
 *     mechanism (FEAT_OVR_SM_SPD_0/1 at 0x82381c/0x823820, governed by
 *     FEAT_OVR_PLM at 0x823804) for the whole GA100/GA102 family, same
 *     registers and same target values the AD103 reference's own doc
 *     describes for Ada -- shared SEC2/Booter firmware across the family,
 *     same conclusion profile.c's own header comment already draws about
 *     the FWSEC/Booter geometry matching AD103's despite being pinned
 *     independently from this board's own VBIOS.
 *
 *   - GuAd103ColdEntry() -> GuGa102ColdEntry() throughout.
 *
 * STATUS: complete, not yet hardware-exercised. This is the first file in
 * the port that mutates anything -- GuGa102Release() calls
 * DisconnectController twice (the edid-discovery driver, then the GOP
 * driver, on the physical GOP), then disconnects the GPU's own PCI_IO/
 * NVIDIA driver and, via ClaimRestore(), opens the GPU's PCI_IO protocol
 * EFI_OPEN_PROTOCOL_EXCLUSIVE and enables whatever I/O/memory decode bits
 * were left disabled; GuGa102Finish() calls CloseProtocol to release that
 * claim afterwards. Both re-verify the full protocol graph and this board's
 * cold-entry state between every step and refuse on any mismatch (see
 * Expected()/GuGa102ColdEntry()/ConfigStable() throughout). Still true of
 * everything here: no PCI config-space write, no BAR0 write, no FLR --
 * those live in port.c/vendor/unlock_610.c, gated behind
 * Gu.armed/Gu.owned/Gu.profile_verified, and are reached only from app.c,
 * only after a human presses "U" at GuBootChoose()'s prompt.
 */
#include "ga102_release.h"
#include "boot_io.h"
#include "profile.h"
#include "sha256.h"
#include "compat/compat.h"
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <IndustryStandard/Acpi10.h>

/* 1024, not the AD103 reference's own 4096: Original/Current below are
 * static (persisted across the separate GuGa102Preflight()/Release()/
 * Released()/Finish() calls, unlike probe/'s own one-shot GU_GRAPH, which
 * is a local/stack variable), so at 4096 the two of them together put
 * ~384KB of BSS into this image's own PE data directory. This board's own
 * firmware (ASUS ROG STRIX Z790-A) failed to load the resulting
 * GA102-EXPERIMENTAL.EFI at all -- a near-instant bounce back to the boot
 * picker, before any code ever ran, independent of Secure Boot state --
 * confirmed by dumping the linked PE (`dumpbin /headers`): .data virtual
 * size 0x61600 against a raw size of only 0x200, i.e. ~393KB the loader
 * has to zero-fill on top of what's actually in the file. probe/'s own
 * working binary has a .data virtual/raw gap of about 1.25KB, not 393KB.
 * This board's own captured graph is 144 edges (both cold boots); 1024
 * keeps >7x headroom while cutting this image's BSS footprint ~4x from
 * the reference's own cap. Snapshot() below already fails closed
 * (EFI_COMPROMISED_DATA) if the live graph ever exceeds this, so lowering
 * it only makes an oversized boot configuration refuse more easily, not
 * silently truncate anything. */
#define MAX_EDGES 1024
typedef struct {EFI_HANDLE handle,agent,controller;EFI_GUID protocol;UINT32 attributes,count;} EDGE;
typedef struct {UINTN count;EDGE edges[MAX_EDGES];} GRAPH;
typedef struct {
  EFI_SYSTEM_TABLE *table;EFI_HANDLE self,pci,gop,gop_child,edid_agent,gfx_agent,nv_agent;
  EFI_HANDLE console,input,error,pointer_agent,key_agent,usb_host,usb_agent;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text,*err;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *keys;
  UINTN key_reset,key_read;EFI_EVENT key_wait;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  UINT32 config[64];UINT64 attributes;
  UINTN stage,reported_additions,reported_removals;BOOLEAN ready,mutated,claimed;
} PLAN;
static PLAN Plan;
static GRAPH Original,Current;
static BOOLEAN OriginalUsb[MAX_EDGES];
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static EFI_GUID InputGuid=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
/* This board's own captured BY_DRIVER protocol on the physical GOP in place
 * of the AD103 reference's SimpleTextOut out_agent -- see header comment. */
static EFI_GUID EdidGuid={0x1c0c34f6,0xd380,0x41fa,{0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}};
/* Generic UEFI spec GUIDs, no vendored named macro for either -- same raw
 * literals the AD103 reference itself uses. */
static EFI_GUID PointerGuid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};
static EFI_GUID UsbHcGuid={0x3e745226,0x9818,0x45b6,{0xa2,0xac,0xd7,0xcd,0x0e,0x8b,0xa2,0xbc}};
/* This board's own captured USB host-controller device path (probe/,
 * 2026-11-10/11): ACPI(hid=0x0a0341d0,uid=0) / PCI(device=0x14,function=0).
 * Byte-identical to the AD103 reference's own constant -- both are Z790/PCH
 * platforms and the PCH's built-in xHCI controller sits at the same
 * location on both; confirmed against this board's own capture, not
 * assumed from the match. */
static const UINT8 UsbHostPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,0x14,127,255,4,0};
static EFI_GUID BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
/* This board's own captured GPU PCI_IO device path: ACPI(hid=0x0a0341d0,
 * uid=0) / PCI(1,0) / PCI(0,0) -- confirmed byte-identical across both
 * cold boots (GA102-DIAG-4.TXT/-5.TXT). */
static const UINT8 PciPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,127,255,4,0};
/* Same prefix as PciPath, plus one more node: ACPI_ADR(0x80010340) -- the
 * physical GOP handle's own path, same two captures. */
static const UINT8 GopPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,0x40,3,1,0x80,127,255,4,0};
/* NvFile: this board's own captured LoadedImage FilePath -- NVIDIA's own
 * Ampere GOP driver filename ("nvgop-ga1xx"), embedded in the VBIOS Option
 * ROM. GfxFile/EdidFile: this board's own captured GUID-keyed
 * FirmwareVolumeFile device-path nodes (not decoded further here; see the
 * raw bytes). None of the three is copied from the AD103 reference. */
static const UINT8 NvFile[]={4,4,28,0,'n',0,'v',0,'g',0,'o',0,'p',0,'-',0,'g',0,'a',0,'1',0,'x',0,'x',0,0,0,127,255,4,0};
static const UINT8 GfxFile[]={4,6,20,0,0x62,0x7e,0x98,0x55,0xf6,0x1d,0xd1,0x41,0x90,0x62,0x33,0x4f,0xca,0xc3,0xe5,0x4d,127,255,4,0};
static const UINT8 EdidFile[]={4,6,20,0,0x0c,0x03,0xa7,0xc7,0xd8,0xc3,0xee,0x45,0xbe,0xd9,0x5d,0x9e,0x76,0x76,0x29,0x53,127,255,4,0};

static BOOLEAN Equal(const VOID *a,const VOID *b,UINTN n){return !memcmp(a,b,n);}
static BOOLEAN Guid(const EFI_GUID *a,const EFI_GUID *b){return Equal(a,b,16);}
static EFI_STATUS Interface(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;EFI_STATUS s=Plan.table->BootServices->HandleProtocol(h,g,p);
  return s==EFI_SUCCESS&&!*p?EFI_COMPROMISED_DATA:s;
}
/* Match exact, bounded, single-instance paths; never accept a prefix alone. */
static BOOLEAN Path(EFI_HANDLE h,const UINT8 *bytes,UINTN count) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS)return FALSE;
  UINTN at=0;
  while(at+4<=count) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>count-at||!Equal(p,bytes+at,n))return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE)return n==4&&p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at+4==count;
    at+=n;p=(VOID *)((UINT8 *)p+n);
  }
  return FALSE;
}
static EFI_STATUS Snapshot(GRAPH *g) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *handles=NULL;UINTN count=0;g->count=0;
  EFI_STATUS s=b->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles);
  if(s==EFI_SUCCESS&&(!handles||!count||count>4096))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GUID **protocols=NULL;UINTN pc=0;
    s=b->ProtocolsPerHandle(handles[i],&protocols,&pc);
    if(s==EFI_SUCCESS&&(!protocols||!pc||pc>64))s=EFI_BAD_BUFFER_SIZE;
    for(UINTN j=0;s==EFI_SUCCESS&&j<pc;j++) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN ec=0;
      if(!protocols[j]){s=EFI_COMPROMISED_DATA;break;}
      s=b->OpenProtocolInformation(handles[i],protocols[j],&entries,&ec);
      if(s==EFI_SUCCESS&&(ec>128||(ec&&!entries)))s=EFI_BAD_BUFFER_SIZE;
      for(UINTN k=0;s==EFI_SUCCESS&&k<ec;k++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[k];
        if(e->Attributes==1||e->Attributes==2||e->Attributes==4)continue;
        BOOLEAN own=Plan.claimed&&handles[i]==Plan.pci&&e->AgentHandle==Plan.self&&
          !e->ControllerHandle&&e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->OpenCount==1;
        if(g->count==MAX_EDGES||!e->AgentHandle||(!e->ControllerHandle&&!own)||!e->OpenCount){s=EFI_COMPROMISED_DATA;break;}
        g->edges[g->count++]=(EDGE){handles[i],e->AgentHandle,e->ControllerHandle,*protocols[j],e->Attributes,e->OpenCount};
      }
      if(entries&&b->FreePool(entries)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    }
    if(protocols&&b->FreePool(protocols)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  }
  if(handles&&b->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static BOOLEAN SameEdge(const EDGE *a,const EDGE *b) {
  return a->handle==b->handle&&a->agent==b->agent&&a->controller==b->controller&&
    a->attributes==b->attributes&&a->count==b->count&&Guid(&a->protocol,&b->protocol);
}
/* which: 0=edid, 1=gfx, 2=nv. Stage 1 detaches edid_agent, stage 2 detaches
 * gfx_agent (both on the physical GOP), stage 3 detaches nv_agent (on the
 * GPU's own PCI_IO) and claims it exclusively. The stage>=2 branch matches
 * both of gfx_agent's edges on Plan.gop regardless of attributes/
 * controller -- its primary BY_DRIVER edge AND its secondary
 * BY_CHILD_CONTROLLER edge naming Plan.gop_child (see header comment) --
 * same loose-match style the AD103 reference itself uses for its own
 * stage-2 branch.
 *
 * The third stage>=2 line was, through RUN0011.TXT, "remove the whole
 * Plan.gop_child subtree too" -- that assumption is now known wrong (see
 * header comment: five real hardware runs confirmed none of
 * Plan.gop_child's own three edges are ever retracted). There is no
 * third line any more: the only thing stage 2 actually removes is
 * gfx_agent's own edge on Plan.gop, already covered by the line above.
 * Plan.gop_child's edges are ordinary, still-present edges now, checked
 * by Expected()/GraphDiff() the same way as everything else in the
 * graph, nothing special. */
static BOOLEAN Removed(const EDGE *e,UINTN stage) {
  if(stage>=1&&e->handle==Plan.gop&&e->agent==Plan.edid_agent&&Guid(&e->protocol,&EdidGuid))return TRUE;
  if(stage>=2&&e->handle==Plan.gop&&e->agent==Plan.gfx_agent&&Guid(&e->protocol,&GopGuid))return TRUE;
  return stage>=3&&e->handle==Plan.pci&&e->agent==Plan.nv_agent;
}
static BOOLEAN ProtectedHandle(EFI_HANDLE h) {
  return h&&(h==Plan.pci||h==Plan.gop||h==Plan.self||h==Plan.edid_agent||
    h==Plan.gfx_agent||h==Plan.nv_agent||h==Plan.console||h==Plan.input||h==Plan.error);
}
static BOOLEAN ProtectedEdge(const EDGE *e) {
  return ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||ProtectedHandle(e->controller);
}
static BOOLEAN OwnEdge(const EDGE *e) {
  return e->handle==Plan.pci&&e->agent==Plan.self&&!e->controller&&
    Guid(&e->protocol,&PciGuid)&&e->attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->count==1;
}
static BOOLEAN KnownEdge(const EDGE *e,UINTN stage) {
  for(UINTN i=0;i<Original.count;i++)
    if(!Removed(&Original.edges[i],stage)&&SameEdge(&Original.edges[i],e))return TRUE;
  return FALSE;
}
static BOOLEAN UsbPointerPath(EFI_HANDLE h) {
  static const UINT8 host[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,0x14};
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS||!Equal(p,host,sizeof(host)))return FALSE;
  UINT8 *at=(UINT8 *)p+sizeof(host);
  for(UINTN depth=0;depth<8;depth++) {
    if(at[0]!=MESSAGING_DEVICE_PATH||at[1]!=MSG_USB_DP||at[2]!=6||at[3]!=0)return FALSE;
    at+=6;
    if(at[0]==END_DEVICE_PATH_TYPE)return at[1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at[2]==4&&at[3]==0;
  }
  return FALSE;
}
static BOOLEAN PointerOwner(const GRAPH *g,const EDGE *e) {
  UINTN owners=0;
  for(UINTN i=0;i<g->count;i++) {
    const EDGE *o=&g->edges[i];
    if(o->handle==e->handle&&Guid(&o->protocol,&e->protocol)&&
       o->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER) {
      if(o->agent!=e->agent||o->controller!=e->handle||o->count!=1)return FALSE;
      owners++;
    }
  }
  return owners==1;
}
static BOOLEAN PointerAddition(const EDGE *e) {
  BOOLEAN splitter=(Guid(&e->protocol,&PointerGuid)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&
    !ProtectedHandle(e->handle)&&!ProtectedHandle(e->agent)&&
    e->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER&&e->count==1&&
    PointerOwner(&Current,e)&&UsbPointerPath(e->handle);
}
static EFI_STATUS PointerBaseline(VOID) {
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    BOOLEAN pointer=Guid(&e->protocol,&PointerGuid),key=Guid(&e->protocol,&InputGuid);
    if((!pointer&&!key)||e->controller!=Plan.console||e->attributes!=8)continue;
    EFI_HANDLE *agent=pointer?&Plan.pointer_agent:&Plan.key_agent;
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||
       !PointerOwner(&Original,e)||!UsbPointerPath(e->handle)||
       (*agent&&e->agent!=*agent))return EFI_UNSUPPORTED;
    *agent=e->agent;
  }
  return EFI_SUCCESS;
}
static BOOLEAN UsbConsoleChild(const EDGE *e,const GRAPH *g) {
  BOOLEAN splitter=(Guid(&e->protocol,&PointerGuid)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&e->attributes==8&&e->count==1&&PointerOwner(g,e);
}
static EFI_STATUS UsbBaseline(VOID) {
  memset(OriginalUsb,0,sizeof(OriginalUsb));
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(!Guid(&e->protocol,&UsbHcGuid)||e->attributes!=0x10||e->controller!=e->handle||
       e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||
       !Path(e->handle,UsbHostPath,sizeof(UsbHostPath)))continue;
    if(Plan.usb_host)return EFI_UNSUPPORTED;
    Plan.usb_host=e->handle;Plan.usb_agent=e->agent;
  }
  if(!Plan.usb_host||!Plan.usb_agent)return EFI_UNSUPPORTED;
  UINTN dynamic=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent))continue;
    BOOLEAN child=e->handle==Plan.usb_host&&e->agent==Plan.usb_agent&&
      Guid(&e->protocol,&UsbHcGuid)&&e->attributes==8&&!ProtectedHandle(e->controller)&&
      UsbPointerPath(e->controller);
    BOOLEAN physical=UsbPointerPath(e->handle);
    BOOLEAN device=physical&&((e->attributes==0x10&&e->controller==e->handle)||
      (e->attributes==8&&!ProtectedHandle(e->controller)&&UsbPointerPath(e->controller))||
      UsbConsoleChild(e,&Original));
    if(child||device){OriginalUsb[i]=TRUE;dynamic++;}
  }
  GuLog("GA102 USB baseline: host=%#llx bus_agent=%#llx dynamic_edges=%zu; host ownership pinned\n",
    (unsigned long long)(UINTN)Plan.usb_host,(unsigned long long)(UINTN)Plan.usb_agent,(size_t)dynamic);
  return EFI_SUCCESS;
}
static BOOLEAN UsbRemoved(UINTN i) {
  if(!OriginalUsb[i])return FALSE;
  const EDGE *e=&Original.edges[i];
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *c=&Current.edges[j];
    if(c->handle==e->handle&&c->agent==e->agent&&c->controller==e->controller&&Guid(&c->protocol,&e->protocol))return FALSE;
  }
  return TRUE;
}
static BOOLEAN Expected(UINTN stage,BOOLEAN claimed) {
  UINTN own=0;
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],stage))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i)))return FALSE;
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(OwnEdge(e)){own++;continue;}
    if(!KnownEdge(e,stage)&&ProtectedEdge(e)&&!PointerAddition(e))return FALSE;
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k]))return FALSE;
  }
  return own==(claimed?1u:0u);
}
#define NOTE(...) do{if(GuBootLogOpen())GuLog(__VA_ARGS__);}while(0)
static VOID NoteEdge(const char *kind,const EDGE *e) {
  NOTE("GA102 graph %s: handle=%#llx agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",kind,
    (unsigned long long)(UINTN)e->handle,(unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,
    e->protocol.Data1,e->attributes,e->count);
}
/* stage parameter added 2026-11-11, after the first real hardware attempt
 * stopped at the post-stage-2 Expected() check with nothing but a bare
 * EFI_NOT_READY in the log -- the stage-loop callers below need to know
 * which stage's Removed()/KnownEdge() to diff against, not just the
 * final stage=3 state GuGa102Released()/GuGa102Finish() care about. */
static VOID GraphDiff(UINTN stage,BOOLEAN claimed) {
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],stage))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i))){NoteEdge(matches?"duplicate":"missing",&Original.edges[i]);return;}
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(!KnownEdge(e,stage)&&!(claimed&&OwnEdge(e))&&ProtectedEdge(e)&&!PointerAddition(e)){NoteEdge("unexpected",e);return;}
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k])){NoteEdge("duplicate",e);return;}
  }
}
static VOID ReportAdditions(VOID) {
  UINTN added=0,removed=0;
  for(UINTN i=0;i<Original.count;i++)if(UsbRemoved(i))removed++;
  if(removed!=Plan.reported_removals&&GuBootLogOpen()) {
    NOTE("GA102 graph: %zu original USB relationships absent; host/GPU ownership and console callbacks unchanged\n",(size_t)removed);
    Plan.reported_removals=removed;
  }
  for(UINTN j=0;j<Current.count;j++)
    if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3))added++;
  if(added&&added!=Plan.reported_additions&&GuBootLogOpen()) {
    NOTE("GA102 graph: accepted %zu new edges (unrelated or verified USB pointer); GPU ownership unchanged\n",(size_t)added);
    for(UINTN j=0,shown=0;j<Current.count&&shown<8;j++)
      if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3)){NoteEdge("new-unrelated",&Current.edges[j]);shown++;}
    Plan.reported_additions=added;
  }
}
/* which: 0=edid, 1=gfx, 2=nv. Every size/stop/code/bytes/hash/version value
 * below is this board's own capture from probe/ (2026-11-10/11), confirmed
 * identical across two separate genuine cold boots, not the AD103
 * reference's. The hashed code range is this board's own PE-header-
 * identified .text section for each driver, not a hand-picked offset. */
static EFI_STATUS Driver(UINTN which) {
  EFI_HANDLE a=which==0?Plan.edid_agent:which==1?Plan.gfx_agent:Plan.nv_agent;
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(a,&BindingGuid,(VOID **)&d);
  UINT32 version=which==0?0x1u:which==1?0x11u:0x60010u;
  if(s!=EFI_SUCCESS||!d->Stop||d->DriverBindingHandle!=a||!d->ImageHandle||
     d->Version!=version)return EFI_UNSUPPORTED;
  s=Interface(d->ImageHandle,&ImageGuid,(VOID **)&im);if(s!=EFI_SUCCESS)return s;
  UINTN size=which==0?36864:which==1?8576:158432;
  UINTN stop=which==0?0x14d4:which==1?0x88c:0x678;
  UINTN code=which==0?0x1000:which==1?0x2a0:0x280;
  UINTN bytes=which==0?0x37da:which==1?0x1990:0x20d33;
  /* The edid driver (which==0) loads as EfiRuntimeServicesCode (5); nv/gfx
   * load as EfiBootServicesCode (3) -- confirmed both runs, not a typo. */
  EFI_MEMORY_TYPE codeType=which==0?EfiRuntimeServicesCode:EfiBootServicesCode;
  const char *hash=which==0?"a015d52f55fa6441414458e2b699ba2edcb53f03b001ac6367e45bcd908c418e":
    which==1?"3f4c4ea26023ddf00990b59385d0fcf3139acb583c70406828777cdceab8cecb":
    "615b37a708b1894e18d7fa60cfcdacaa4d8b598863a10b8103d231a595847b2b";
  const UINT8 *file=which==0?EdidFile:which==1?GfxFile:NvFile;
  UINTN file_bytes=which==0?sizeof(EdidFile):which==1?sizeof(GfxFile):sizeof(NvFile);
  if(!im->ImageBase||im->ImageSize!=size||im->ImageCodeType!=codeType||
     (UINTN)d->Stop!=(UINTN)im->ImageBase+stop||!im->FilePath||
     !Equal(im->FilePath,file,file_bytes)||code+bytes>size||
     (which==2&&(im->DeviceHandle!=Plan.pci||d->ImageHandle!=a)))return EFI_UNSUPPORTED;
  return GuHashMatches((UINT8 *)im->ImageBase+code,bytes,hash)?EFI_SUCCESS:EFI_CRC_ERROR;
}
static EFI_STATUS Alive(VOID) {
  EFI_SYSTEM_TABLE *t=Plan.table;VOID *in=NULL,*out=NULL,*err=NULL;
  if(t->ConsoleOutHandle!=Plan.console||t->ConsoleInHandle!=Plan.input||t->StandardErrorHandle!=Plan.error||
     t->ConIn!=Plan.keys||t->ConOut!=Plan.text||t->StdErr!=Plan.err||!t->ConIn||
     (UINTN)t->ConIn->Reset!=Plan.key_reset||(UINTN)t->ConIn->ReadKeyStroke!=Plan.key_read||
     t->ConIn->WaitForKey!=Plan.key_wait)return EFI_NOT_READY;
  EFI_STATUS s=Interface(Plan.input,&InputGuid,&in);
  if(s==EFI_SUCCESS)s=Interface(Plan.console,&TextGuid,&out);
  if(s==EFI_SUCCESS)s=Interface(Plan.error,&TextGuid,&err);
  return s==EFI_SUCCESS&&(in!=Plan.keys||out!=Plan.text||err!=Plan.err)?EFI_NOT_READY:s;
}
/* Finds the physical GOP (Plan.gop, device-path matched) and, while it's
 * still bound (Plan.stage<2), this board's own confirmed second
 * GOP-protocol handle (Plan.gop_child -- see header comment and
 * Consumers() below): not device-path matchable, not the console, exactly
 * one of them. Once gfx_agent is detached (Plan.stage>=2) this function no
 * longer requires any particular GOP shape at all, mirroring the AD103
 * reference's own stage>=3 relaxation (shifted by one stage here, since
 * this board's GOP-side work finishes at stage 2, not 3). */
static EFI_STATUS Gops(BOOLEAN first) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *h=NULL;UINTN count=0,physical=0,other=0;
  EFI_HANDLE child=NULL;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&h);
  if(Plan.stage>=2&&s==EFI_NOT_FOUND)return EFI_SUCCESS;
  if(s==EFI_SUCCESS&&(count>2||(count&&!h)))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;s=Interface(h[i],&GopGuid,(VOID **)&g);if(s!=EFI_SUCCESS)break;
    if(Plan.stage>=2||!Path(h[i],GopPath,sizeof(GopPath))) {
      if(Plan.stage<2) {
        if(child||++other!=1){s=EFI_UNSUPPORTED;break;}
        child=h[i];
      }
      continue;
    }
    if(++physical!=1||!g->Mode||!g->Mode->Info||g->Mode->SizeOfInfo<sizeof(*g->Mode->Info)||
       !g->Mode->FrameBufferBase||!g->Mode->FrameBufferSize){s=EFI_UNSUPPORTED;break;}
    if(first){Plan.gop=h[i];Plan.gop_interface=g;Plan.mode=*g->Mode;Plan.info=*g->Mode->Info;}
    else if(h[i]!=Plan.gop||g!=Plan.gop_interface||!Equal(g->Mode,&Plan.mode,sizeof(Plan.mode))||!Equal(g->Mode->Info,&Plan.info,sizeof(Plan.info)))s=EFI_NOT_READY;
  }
  if(h&&b->FreePool(h)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS&&Plan.stage<2) {
    if(physical!=1||other!=1)s=EFI_NOT_READY;
    else if(first)Plan.gop_child=child;
    else if(child!=Plan.gop_child)s=EFI_NOT_READY;
  }
  return s;
}
static EFI_STATUS ReadConfig(UINT32 c[64]) {
  EFI_PCI_IO_PROTOCOL *io=NULL;UINTN seg,bus,dev,fun;
  EFI_STATUS s=Interface(Plan.pci,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS||io!=Gu.pci||!io->Pci.Read||!io->GetLocation)return EFI_NOT_READY;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS||seg||bus!=1||dev||fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s==EFI_SUCCESS&&(!GuTargetConfig(c,seg,bus,dev,fun)||(c[1]&4)))s=EFI_UNSUPPORTED;
  return s;
}
static BOOLEAN ConfigStable(const UINT32 c[64]) {
  for(UINTN i=0;i<64;i++) {
    UINT32 drift=c[i]^Plan.config[i];
    if(!drift)continue;
    BOOLEAN allowed=Gu.flrs==1&&i==0x80/4&&Plan.config[0x78/4]==0x0012b410&&
      !(c[i]&~Plan.config[i])&&!(drift&~0x00090000u);
    if(!allowed) {
      NOTE("PCI config drift refused: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
        (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
      return FALSE;
    }
    NOTE("PCI status drift tolerated: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
      (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
  }
  return TRUE;
}
/* AddrRangeMax = GU_BAR0_BASE+0xFFFFFF, recomputed for this board's own
 * BAR0_BASE (0x84000000 -> 0x84FFFFFF), not the AD103 reference's literal
 * (0x81000000 -> 0x81FFFFFF). */
static EFI_STATUS Bar(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=NULL;UINT64 support=0;
  if(!Gu.pci->GetBarAttributes)return EFI_UNSUPPORTED;
  EFI_STATUS s=Gu.pci->GetBarAttributes(Gu.pci,0,&support,(VOID **)&d);
  if(s==EFI_SUCCESS&&(!d||d->Desc!=0x8a||d->Len!=43||d->ResType||
     d->AddrRangeMin!=GU_BAR0_BASE||d->AddrRangeMax!=0x84ffffffULL||
     d->AddrTranslationOffset||d->AddrLen!=0x1000000))s=EFI_UNSUPPORTED;
  if(d&&Plan.table->BootServices->FreePool(d)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
/* Classifies every edge on Plan.pci and Plan.gop. This board's own capture
 * (confirmed both runs): pci child==1 (just Plan.gop, no second PCI-side
 * child the way the AD103 reference's own board has); gop carries gfx==1,
 * edid==1, plus exactly one extra gfx_agent BY_CHILD_CONTROLLER edge naming
 * Plan.gop_child (see header comment) -- no text/virtual tallies exist on
 * this board's physical GOP at all (nothing BY_DRIVER or BY_CHILD on
 * SimpleTextOut is ever opened there), so unlike the AD103 reference's own
 * Consumers() there is no TextGuid branch here; any TextGuid edge on
 * Plan.gop would fall through to the fail-closed else below. */
static EFI_STATUS Consumers(VOID) {
  UINTN pci=0,child=0,gfx=0,edid=0,gop_child_edges=0;
  EFI_HANDLE gop_child_agent=NULL,gop_child_controller=NULL;
  GuLog("Consumers: pci=%#llx gop=%#llx gop_child=%#llx console=%#llx\n",
    (unsigned long long)(UINTN)Plan.pci,(unsigned long long)(UINTN)Plan.gop,
    (unsigned long long)(UINTN)Plan.gop_child,(unsigned long long)(UINTN)Plan.console);
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci) {
      if(e->count!=1) {
        GuLog("Consumers: FAIL pci-edge count!=1 agent=%#llx protocol=%#x attributes=%#x count=%u\n",
          (unsigned long long)(UINTN)e->agent,e->protocol.Data1,e->attributes,e->count);
        return EFI_ACCESS_DENIED;
      }
      if(Guid(&e->protocol,&PciGuid)&&e->attributes==0x10&&e->controller==Plan.pci){Plan.nv_agent=e->agent;pci++;}
      else if(Guid(&e->protocol,&PathGuid)&&e->attributes==8&&e->controller==Plan.gop){child++;}
      else {
        GuLog("Consumers: FAIL unclassified pci-edge agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",
          (unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,e->protocol.Data1,e->attributes,e->count);
        return EFI_ACCESS_DENIED;
      }
    } else if(e->handle==Plan.gop) {
      if(e->count!=1) {
        GuLog("Consumers: FAIL gop-edge count!=1 agent=%#llx protocol=%#x attributes=%#x count=%u\n",
          (unsigned long long)(UINTN)e->agent,e->protocol.Data1,e->attributes,e->count);
        return EFI_ACCESS_DENIED;
      }
      if(Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.gfx_agent=e->agent;gfx++;}
      else if(Guid(&e->protocol,&EdidGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.edid_agent=e->agent;edid++;}
      else if(Guid(&e->protocol,&GopGuid)&&e->attributes==8&&Plan.gop_child&&e->controller==Plan.gop_child) {
        gop_child_edges++;gop_child_agent=e->agent;gop_child_controller=e->controller;
      } else {
        GuLog("Consumers: FAIL unclassified gop-edge agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",
          (unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,e->protocol.Data1,e->attributes,e->count);
        return EFI_ACCESS_DENIED;
      }
    }
  }
  GuLog("Consumers: tallies pci=%u child=%u gfx=%u edid=%u gop_child_edges=%u nv_agent=%#llx gfx_agent=%#llx edid_agent=%#llx\n",
    (unsigned)pci,(unsigned)child,(unsigned)gfx,(unsigned)edid,(unsigned)gop_child_edges,
    (unsigned long long)(UINTN)Plan.nv_agent,(unsigned long long)(UINTN)Plan.gfx_agent,(unsigned long long)(UINTN)Plan.edid_agent);
  if(pci!=1||child!=1||gfx!=1||edid!=1||gop_child_edges!=1||!Plan.gop_child||
     gop_child_agent!=Plan.gfx_agent||gop_child_controller!=Plan.gop_child||
     Plan.nv_agent==Plan.gfx_agent||Plan.nv_agent==Plan.edid_agent||Plan.gfx_agent==Plan.edid_agent) {
    GuLog("Consumers: FAIL tally check (see tallies line above)\n");
    return EFI_ACCESS_DENIED;
  }
  /* Plan.gop_child itself (this board's own confirmed second GOP-protocol
   * handle, found independently by Gops() via protocol enumeration) must
   * carry exactly the three edges both captures found and nothing else --
   * see header comment for what DisconnectController does to them, which
   * this file does not assume either way. */
  UINTN childSelf=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle!=Plan.gop_child)continue;
    if(e->count!=1) {
      GuLog("Consumers: FAIL gop_child-edge count!=1 agent=%#llx protocol=%#x attributes=%#x count=%u\n",
        (unsigned long long)(UINTN)e->agent,e->protocol.Data1,e->attributes,e->count);
      return EFI_ACCESS_DENIED;
    }
    BOOLEAN selfText=Guid(&e->protocol,&TextGuid)&&e->attributes==0x10&&e->controller==Plan.gop_child;
    BOOLEAN splitText=Guid(&e->protocol,&TextGuid)&&e->attributes==8&&e->controller==Plan.console;
    BOOLEAN selfGop=Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&e->controller==Plan.gop_child;
    if(!selfText&&!splitText&&!selfGop) {
      GuLog("Consumers: FAIL unclassified gop_child-edge agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",
        (unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,e->protocol.Data1,e->attributes,e->count);
      return EFI_ACCESS_DENIED;
    }
    childSelf++;
  }
  if(childSelf!=3) {
    GuLog("Consumers: FAIL gop_child edge count=%u (want 3)\n",(unsigned)childSelf);
    return EFI_ACCESS_DENIED;
  }
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci&&e->agent!=Plan.nv_agent) {
      GuLog("Consumers: FAIL second pci pass agent=%#llx!=nv_agent=%#llx\n",
        (unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)Plan.nv_agent);
      return EFI_ACCESS_DENIED;
    }
  }
  for(UINTN i=0;i<3;i++){EFI_STATUS s=Driver(i);if(s!=EFI_SUCCESS){GuLog("Consumers: FAIL Driver(%u)=%#llx\n",(unsigned)i,(unsigned long long)s);return s;}}
  return EFI_SUCCESS;
}
EFI_STATUS GuGa102Preflight(EFI_SYSTEM_TABLE *t,EFI_HANDLE self,EFI_HANDLE pci) {
  if(Plan.mutated||Plan.ready)return EFI_ALREADY_STARTED;
  memset(&Plan,0,sizeof(Plan));Plan.table=t;Plan.self=self;Plan.pci=pci;
  if(!t||!t->BootServices||!self||!pci||Gu.handle!=pci||Gu.image!=self||Gu.bs!=t->BootServices||
     !Gu.profile_verified||!Gu.prom_verified||Gu.started||Gu.owned||Gu.armed)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *b=t->BootServices;
  if(!b->DisconnectController||!b->OpenProtocol||!b->CloseProtocol||!b->RaiseTPL||!b->RestoreTPL||
     !b->ProtocolsPerHandle||!b->OpenProtocolInformation||!b->LocateHandleBuffer||!b->HandleProtocol)return EFI_UNSUPPORTED;
  Plan.console=t->ConsoleOutHandle;Plan.input=t->ConsoleInHandle;Plan.error=t->StandardErrorHandle;
  Plan.text=t->ConOut;Plan.err=t->StdErr;Plan.keys=t->ConIn;
  if(!Plan.console||Plan.console!=Plan.input||Plan.console!=Plan.error||!Plan.text||!Plan.err||!Plan.keys||
     !Plan.keys->ReadKeyStroke||!Plan.keys->Reset||!Plan.keys->WaitForKey)return EFI_UNSUPPORTED;
  Plan.key_reset=(UINTN)Plan.keys->Reset;Plan.key_read=(UINTN)Plan.keys->ReadKeyStroke;Plan.key_wait=Plan.keys->WaitForKey;
  if(!Path(pci,PciPath,sizeof(PciPath))||!GuGa102ColdEntry())return EFI_NOT_READY;
  EFI_STATUS s=Gops(TRUE);
  if(s==EFI_SUCCESS)s=Alive();
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS)s=ReadConfig(Plan.config);
  if(s==EFI_SUCCESS)s=Gu.pci->Attributes?Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&Plan.attributes):EFI_UNSUPPORTED;
  UINT64 decode=(Plan.config[1]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(Plan.config[1]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  if(s==EFI_SUCCESS&&(Plan.attributes&(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))!=decode)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Original);
  if(s==EFI_SUCCESS)s=Consumers();
  if(s==EFI_SUCCESS)s=PointerBaseline();
  if(s==EFI_SUCCESS)s=UsbBaseline();
  GuLog("GA102 preflight: status=%#llx graph=%zu code_profiles=3\n",(unsigned long long)s,(size_t)Original.count);
  if(s==EFI_SUCCESS)GuLog("GA102 endpoints: pci=%#llx gop=%#llx gop_child=%#llx console=%#llx pointer_agent=%#llx\n",
    (unsigned long long)(UINTN)Plan.pci,(unsigned long long)(UINTN)Plan.gop,(unsigned long long)(UINTN)Plan.gop_child,
    (unsigned long long)(UINTN)Plan.console,(unsigned long long)(UINTN)Plan.pointer_agent);
  if(s==EFI_SUCCESS)Plan.ready=TRUE;return s;
}

static BOOLEAN DecodeMask(const UINT32 c[64],UINT64 *mask) {
  *mask=0;UINT32 missing=Plan.config[1]&~c[1]&3;
  if((Plan.config[1]&6)!=2||(c[1]&4))return FALSE;
  for(UINTN i=0;i<64;i++)if((c[i]|(i==1?missing:0))!=Plan.config[i])return FALSE;
  *mask=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);return TRUE;
}
/* The only place in this port that claims anything exclusively: opens the
 * GPU's PCI_IO protocol EFI_OPEN_PROTOCOL_EXCLUSIVE and enables whatever
 * I/O or memory decode bits the stock driver had left disabled. Every step
 * is checked against the Original graph/config snapshot before and after;
 * any mismatch leaves Gu.owned/Plan.claimed set (so callers can't assume
 * it's safe to pretend this didn't happen) but returns an error. */
static EFI_STATUS ClaimRestore(VOID) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_PCI_IO_PROTOCOL *io=NULL;
  UINT32 c[64];UINT64 mask=0,attrs=0,supported=0;
  EFI_STATUS s=ReadConfig(c);if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  GuLog("GA102 decode restore intent: status=%#llx IO_MEM_mask=%#llx\n",(unsigned long long)s,(unsigned long long)mask);
  EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(3,FALSE)){NOTE("GA102 claim-intent graph mismatch\n");GraphDiff(3,FALSE);s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS)s=b->OpenProtocol(Plan.pci,&PciGuid,(VOID **)&io,Plan.self,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(s==EFI_SUCCESS){Plan.claimed=TRUE;Gu.owned=TRUE;if(io!=Gu.pci)s=EFI_COMPROMISED_DATA;}
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=(Plan.attributes&~mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationSupported,0,&supported);
  if(s==EFI_SUCCESS&&(supported&mask)!=mask)s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(3,TRUE)){NOTE("GA102 post-claim graph mismatch\n");GraphDiff(3,TRUE);s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS&&mask)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,mask,NULL);
  b->RestoreTPL(old);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=Plan.attributes)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS&&!GuGa102ColdEntry())s=EFI_NOT_READY;
  GuLog("GA102 exclusive claim/decode result: status=%#llx\n",(unsigned long long)s);return s;
}
/* Detaches the two stock display drivers (edid-discovery, then GOP, both on
 * the physical GOP), then the GPU's own PCI_IO/NVIDIA driver, re-verifying
 * the graph and this exact board's cold-entry state between every stage,
 * then claims the GPU via ClaimRestore above. Any failure at any stage
 * returns immediately -- Plan.mutated is set the moment the first
 * DisconnectController call is made, so callers can tell "nothing touched
 * yet" from "partway through" even on failure. */
EFI_STATUS GuGa102Release(VOID) {
  if(!Plan.ready||Plan.mutated||Gu.started||Gu.owned)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  for(UINTN stage=1;stage<=3;stage++) {
    GuLog("GA102 driver detach intent: stage=%zu\n",(size_t)stage);
    EFI_STATUS s=Alive();UINT32 c[64];
    if(s==EFI_SUCCESS)s=ReadConfig(c);
    if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS&&!GuGa102ColdEntry())s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
    if(s==EFI_SUCCESS)s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(stage-1,FALSE)){NOTE("GA102 pre-disconnect graph mismatch: stage=%zu\n",(size_t)stage);GraphDiff(stage-1,FALSE);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS)s=Driver(stage-1);
    b->RestoreTPL(old);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    /* Record before the call: an error may follow a partial driver stop. */
    Plan.mutated=TRUE;
    s=b->DisconnectController(stage==3?Plan.pci:Plan.gop,
      stage==1?Plan.edid_agent:stage==2?Plan.gfx_agent:Plan.nv_agent,NULL);
    GuLog("GA102 driver detach return: stage=%zu status=%#llx\n",(size_t)stage,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=stage;
    /* 2026-11-11: the first real hardware attempt stopped right here
     * (stage 2, i.e. after a *successful* DisconnectController return)
     * with nothing but a bare EFI_NOT_READY -- these three NOTE/GraphDiff
     * calls are what a repeat of that failure needs to actually name the
     * mismatched edge or explain which of console/GOP state moved,
     * instead of just a status code. */
    s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(stage,FALSE)){NOTE("GA102 post-disconnect graph mismatch: stage=%zu\n",(size_t)stage);GraphDiff(stage,FALSE);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS&&Alive()!=EFI_SUCCESS){NOTE("GA102 post-disconnect console check failed: stage=%zu\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS&&Gops(FALSE)!=EFI_SUCCESS){NOTE("GA102 post-disconnect GOP check failed: stage=%zu\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s!=EFI_SUCCESS)return s;
  }
  EFI_STATUS s=ClaimRestore();
  if(s==EFI_SUCCESS&&!GuGa102Released())s=EFI_NOT_READY;
  return s;
}

BOOLEAN GuGa102Released(VOID) {
  UINT32 c[64];UINT64 attrs=0;const char *failed=NULL;
  if(!Plan.ready||Plan.stage!=3||!Plan.claimed||!Gu.owned||Gu.fault)failed="state";
  else if(Alive()!=EFI_SUCCESS)failed="console";
  else if(Gops(FALSE)!=EFI_SUCCESS)failed="GOP";
  else if(ReadConfig(c)!=EFI_SUCCESS)failed="config-read";
  else if(!ConfigStable(c))failed="config-drift";
  else if(Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attrs)!=EFI_SUCCESS||attrs!=Plan.attributes)failed="attributes";
  else if(Snapshot(&Current)!=EFI_SUCCESS)failed="graph-snapshot";
  else if(!Expected(3,TRUE)){failed="graph";GraphDiff(3,TRUE);}
  if(failed)NOTE("GA102 release check failed: %s attributes=%#llx expected=%#llx graph=%zu original=%zu fault=%u flrs=%u\n",
    failed,(unsigned long long)attrs,(unsigned long long)Plan.attributes,(size_t)Current.count,(size_t)Original.count,Gu.fault,Gu.flrs);
  if(!failed)ReportAdditions();
  return !failed;
}
/* Releases the exclusive PCI_IO claim ClaimRestore took, once the
 * transaction (whatever calls this after GuGa102Release) is done. Only
 * reachable if GuSafeToReturn()/Gu.quiesced/Gu.flrs==1/GuGa102Released()
 * all hold -- i.e. only after a transaction that left the GPU in a safe,
 * verified, post-FLR state. */
EFI_STATUS GuGa102Finish(VOID) {
  if(!GuSafeToReturn()||!Gu.quiesced||Gu.flrs!=1||!GuGa102Released()){NOTE("GA102 finish refused before close\n");return EFI_NOT_READY;}
  EFI_STATUS s=Plan.table->BootServices->CloseProtocol(Plan.pci,&PciGuid,Plan.self,NULL);
  if(s!=EFI_SUCCESS){NOTE("GA102 finish close status=%#llx\n",(unsigned long long)s);return s;}
  Plan.claimed=FALSE;Gu.owned=FALSE;
  s=Snapshot(&Current);if(s==EFI_SUCCESS&&!Expected(3,FALSE)){s=EFI_NOT_READY;GraphDiff(3,FALSE);}
  if(s!=EFI_SUCCESS)NOTE("GA102 finish after close status=%#llx graph=%zu\n",(unsigned long long)s,(size_t)Current.count);
  return s;
}
BOOLEAN GuGa102Mutated(VOID){return Plan.mutated;}
BOOLEAN GuGa102ReadyForWindows(VOID) {
  if(!Plan.ready||Plan.stage!=3||Plan.claimed||Gu.owned||Gu.fault||!Gu.quiesced||Gu.flrs!=1||!GuSafeToReturn())return FALSE;
  UINT32 c[64];
  if(Alive()!=EFI_SUCCESS||Gops(FALSE)!=EFI_SUCCESS||ReadConfig(c)!=EFI_SUCCESS||!ConfigStable(c)||
     Snapshot(&Current)!=EFI_SUCCESS||!Expected(3,FALSE))return FALSE;
  UINT32 chip=GuRead32(0),cpu=GuRead32(0x110100),hi=GuRead32(0x1fa828);
  UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),readout=GuRead32(0x823818),plm=GuRead32(0x823804);
  /* chip=0xb72000a1 is this board's own GA102 id (profile.c's Registers()
   * offset-0 pin), not the AD103 reference's AD103 value 0x193000a1. The
   * ss0/ss1/plm targets are the family-wide post-unlock signature (see
   * docs/How Ampere was unlocked.md), not board-specific. */
  return !Gu.fault&&chip==0xb72000a1&&(cpu&0x10)&&cpu!=0xffffffff&&
    (cpu&0xffff0000)!=0xbadf0000&&!(hi>>4)&&ss0==0x88888888&&ss1==8&&
    !(readout&0x07ffffff)&&readout!=0xffffffff&&plm==0xffffffff;
}
