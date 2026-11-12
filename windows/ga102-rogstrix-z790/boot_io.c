/* SPDX-License-Identifier: GPL-2.0-only */
/* USB log/assets only. Windows ESP is opened read-only. No boot variables.
 *
 * Adapted from windows/ad103-z790-en-REFERENCE/boot_io.c. All structural
 * logic (Inspect/GuBootSay/GuLog/GuBootOpen/ReadFile/GuBootLoadAssets/
 * GuBootLoadWindows/GuBootChoose/GuBootClose/GuBootWindows/GuBootHalt) is
 * unchanged. Changes, every one of them this board's own real capture
 * (confirmed earlier in this port, repeated here verbatim), not copied
 * from the reference "by analogy":
 *   - Inspect()'s HARDDRIVE_DEVICE_PATH match: PartitionNumber=1,
 *     PartitionStart=2048, PartitionSize=204800 (disk 2, partition 1,
 *     start LBA 2048, 204800 sectors / 100 MiB on this machine's own M.2
 *     SSD -- read-only Get-Partition query, 2026-11-10). These happen to
 *     be the same numbers as the AD103 reference's own board (standard
 *     Windows ESP alignment/size on a modern GPT install), but independently
 *     confirmed here, not assumed from that coincidence. MBRType==2/
 *     SignatureType==2 (GPT) unchanged -- both boards use GPT.
 *     WindowsPartition itself is unchanged source
 *     (GU_WINDOWS_PARTITION_INITIALIZER), resolving via boot_profile.h ->
 *     local/boot_profile.h to this board's own real ESP partition GUID
 *     (read live from Windows, not committed to this tree -- see
 *     ../.gitignore and local/boot_profile.h's own comment).
 *   - bootmgfw.efi size/hash: 3086680 bytes, sha256
 *     2d60a2203865122dad22772136b3d905167d9dcf926d602bb5f48b3b6668abec --
 *     this machine's own live, elevated, read-only re-capture (2026-11-10,
 *     re-checked a second time immediately before writing this file, per
 *     the AD103 reference's own lesson that Windows services this file
 *     periodically and a stale capture fails closed with
 *     EFI_COMPROMISED_DATA rather than silently passing). Coincidentally
 *     identical to the AD103 reference board's own corrected value -- two
 *     different machines can land on the same Windows Update servicing
 *     level and get a byte-identical boot manager; this was independently
 *     re-read on this machine, not assumed from that coincidence either.
 *   - GuAd103AssetsValid() -> GuGa102AssetsValid().
 *   - Firmware asset names: gpu-unlock/610.43.03/{booter_load,gsp_boot,
 *     gsp_boot_desc}_ga102.bin, v67-ga102-610.43.03.bin (GA102's own
 *     generated/hash-verified assets -- see profile.c's Pins[] table and
 *     README.md's "Firmware assets" section). gsp_ga10x.bin is unchanged
 *     (architecture-shared, not board- or chip-specific).
 *   - USB log/asset directory names AD103-* -> GA102-* (cosmetic; the
 *     internal firmware names profile.c's GuGa102AssetsValid() actually
 *     checks are unchanged -- those are the architecture-pinned
 *     gpu-unlock/610.43.03/... identifiers, shared across the GA10x/AD10x
 *     firmware family).
 *   - On-screen text: version string and the USB log path it names.
 */
#include "boot_io.h"
#include "boot_profile.h"
#include "compat/compat.h"
#include "format.h"
#include "profile.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

static EFI_SYSTEM_TABLE *Table;
static EFI_HANDLE Self,Windows,OwnVolume;
static EFI_FILE_PROTOCOL *Root,*Log;
static BOOLEAN Silent;
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,InfoGuid=EFI_FILE_INFO_ID;
static const EFI_GUID WindowsPartition=GU_WINDOWS_PARTITION_INITIALIZER;
static const CHAR16 WindowsName[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static BOOLEAN Inspect(const EFI_DEVICE_PATH_PROTOCOL *p,UINTN *size,BOOLEAN *usb,BOOLEAN *windows) {
  UINTN at=0,hd=0;*usb=FALSE;*windows=FALSE;
  if(!p)return FALSE;
  for(UINTN nodes=0;nodes<64&&at+4<=4096;nodes++) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>4096-at)return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE) {
      if(n!=4||p->SubType!=END_ENTIRE_DEVICE_PATH_SUBTYPE)return FALSE;
      *size=at+4;return TRUE;
    }
    if(p->Type==MESSAGING_DEVICE_PATH&&p->SubType==MSG_USB_DP&&n==sizeof(USB_DEVICE_PATH))*usb=TRUE;
    if(p->Type==MEDIA_DEVICE_PATH&&p->SubType==MEDIA_HARDDRIVE_DP) {
      if(n!=sizeof(HARDDRIVE_DEVICE_PATH)||++hd!=1)return FALSE;
      const HARDDRIVE_DEVICE_PATH *d=(const VOID *)p;
      if(d->MBRType==2&&d->SignatureType==2&&d->PartitionNumber==1&&d->PartitionStart==2048&&
         d->PartitionSize==204800&&!memcmp(d->Signature,&WindowsPartition,16))*windows=TRUE;
    }
    at+=n;p=(const VOID *)((const UINT8 *)p+n);
  }
  return FALSE;
}
VOID GuBootSay(const CHAR16 *text) {
  if(!Silent&&Table&&Table->ConOut&&Table->ConOut->OutputString)
    Table->ConOut->OutputString(Table->ConOut,(CHAR16 *)text);
}
VOID GuBootSilence(VOID){Silent=TRUE;}
VOID GuLog(const char *format,...) {
  CHAR8 buffer[2048];va_list args;va_start(args,format);
  size_t n=GuFormatV(buffer,sizeof(buffer),format,args);va_end(args);
  if(n==(size_t)-1||n>=sizeof(buffer)||!Log){Gu.fault=TRUE;return;}
  UINTN bytes=n;EFI_STATUS s=Log->Write(Log,&bytes,buffer);
  if(s!=EFI_SUCCESS||bytes!=n||Log->Flush(Log)!=EFI_SUCCESS)Gu.fault=TRUE;
}
VOID GuLogText(const char *text){GuLog("%s",text);}
BOOLEAN GuBootLogOpen(VOID){return Log!=NULL;}
EFI_STATUS GuBootOpen(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(Table)return EFI_ALREADY_STARTED;
  if(!image||!t||!t->BootServices)return EFI_INVALID_PARAMETER;
  Table=t;Self=image;
  EFI_BOOT_SERVICES *b=t->BootServices;EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  EFI_STATUS s=b->HandleProtocol(image,&LoadedGuid,(VOID **)&loaded);
  if(s!=EFI_SUCCESS||!loaded||!loaded->DeviceHandle)return EFI_UNSUPPORTED;
  OwnVolume=loaded->DeviceHandle;s=b->HandleProtocol(OwnVolume,&PathGuid,(VOID **)&path);
  UINTN length=0;BOOLEAN usb=FALSE,windows=FALSE;
  if(s!=EFI_SUCCESS||!Inspect(path,&length,&usb,&windows)||!usb)return EFI_ACCESS_DENIED;
  s=b->HandleProtocol(OwnVolume,&FsGuid,(VOID **)&fs);if(s!=EFI_SUCCESS||!fs)return EFI_UNSUPPORTED;
  s=fs->OpenVolume(fs,&Root);if(s!=EFI_SUCCESS||!Root)return EFI_DEVICE_ERROR;
  EFI_FILE_PROTOCOL *dir=NULL;
  s=Root->Open(Root,&dir,L"\\GA102-UNLOCK",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
  if(s!=EFI_SUCCESS||!dir)return EFI_DEVICE_ERROR;
  for(UINTN slot=1;slot<=9999;slot++) {
    CHAR16 name[]=L"RUN0000.TXT";UINTN number=slot;
    for(UINTN at=7;at>3;){at--;name[at]=(CHAR16)('0'+number%10);number/=10;}
    EFI_FILE_PROTOCOL *existing=NULL;s=dir->Open(dir,&existing,name,EFI_FILE_MODE_READ,0);
    if(s==EFI_SUCCESS&&existing){s=existing->Close(existing);if(s!=EFI_SUCCESS)break;continue;}
    if(s!=EFI_NOT_FOUND)break;
    s=dir->Open(dir,&Log,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);break;
  }
  EFI_STATUS closed=dir->Close(dir);if(s!=EFI_SUCCESS||!Log)return s==EFI_SUCCESS?EFI_OUT_OF_RESOURCES:s;
  if(closed!=EFI_SUCCESS)return closed;
  if(!b->SetWatchdogTimer)return EFI_UNSUPPORTED;
  return b->SetWatchdogTimer(0,0,0,NULL);
}
static EFI_STATUS ReadFile(EFI_FILE_PROTOCOL *root,const CHAR16 *name,UINTN expected,VOID **data) {
  *data=NULL;EFI_FILE_PROTOCOL *f=NULL;UINT64 info_words[128];UINTN info_bytes=sizeof(info_words);
  EFI_STATUS s=root->Open(root,&f,(CHAR16 *)name,EFI_FILE_MODE_READ,0);
  if(s!=EFI_SUCCESS||!f)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  s=f->GetInfo(f,&InfoGuid,&info_bytes,info_words);
  EFI_FILE_INFO *info=(VOID *)info_words;
  if(s==EFI_SUCCESS&&(info_bytes<sizeof(EFI_FILE_INFO)||info_bytes>sizeof(info_words)||
     info->FileSize!=expected||(info->Attribute&EFI_FILE_DIRECTORY)))s=EFI_COMPROMISED_DATA;
  VOID *p=NULL;
  if(s==EFI_SUCCESS){p=GuAlloc(expected);if(!p)s=EFI_OUT_OF_RESOURCES;}
  for(UINTN at=0;s==EFI_SUCCESS&&at<expected;) {
    UINTN n=expected-at;if(n>1048576)n=1048576;UINTN wanted=n;
    s=f->Read(f,&n,(UINT8 *)p+at);if(s==EFI_SUCCESS&&n!=wanted)s=EFI_DEVICE_ERROR;at+=n;
  }
  if(s==EFI_SUCCESS){UINT8 tail;UINTN n=1;s=f->Read(f,&n,&tail);if(s==EFI_SUCCESS&&n)s=EFI_COMPROMISED_DATA;}
  EFI_STATUS closed=f->Close(f);if(s==EFI_SUCCESS)s=closed;
  if(s!=EFI_SUCCESS){GuFree(p);return s;}*data=p;return EFI_SUCCESS;
}
EFI_STATUS GuBootLoadAssets(VOID) {
  static const CHAR16 *names[]={L"\\GA102-ASSETS\\booter_load_ga102.bin",L"\\GA102-ASSETS\\v67-ga102-610.43.03.bin",
    L"\\GA102-ASSETS\\gsp_ga10x.bin",L"\\GA102-ASSETS\\gsp_boot_ga102.bin",L"\\GA102-ASSETS\\gsp_boot_desc_ga102.bin"};
  static const char *full[]={"gpu-unlock/610.43.03/booter_load_ga102.bin","gpu-unlock/610.43.03/v67-ga102-610.43.03.bin",
    "gpu-unlock/610.43.03/gsp_ga10x.bin","gpu-unlock/610.43.03/gsp_boot_ga102.bin","gpu-unlock/610.43.03/gsp_boot_desc_ga102.bin"};
  static const UINTN sizes[]={61256,64000,84277400,24576,84};
  if(!Root||Gu.started)return EFI_NOT_READY;
  for(UINTN i=0;i<5;i++) {
    VOID *p=NULL;EFI_STATUS s=ReadFile(Root,names[i],sizes[i],&p);
    if(s!=EFI_SUCCESS)return s;
    GuAssets[i].name=full[i];GuAssets[i].firmware=(struct firmware){sizes[i],p};
  }
  return GuGa102AssetsValid()?EFI_SUCCESS:EFI_CRC_ERROR;
}
EFI_STATUS GuBootLoadWindows(VOID) {
  if(Windows||!Table||Gu.started)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Table->BootServices;EFI_HANDLE *h=NULL,volume=NULL;UINTN count=0,matches=0,path_bytes=0;
  EFI_DEVICE_PATH_PROTOCOL *base=NULL;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&count,&h);
  if(s==EFI_SUCCESS&&(!h||!count||count>128))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *p=NULL;UINTN bytes=0;BOOLEAN usb=FALSE,windows=FALSE;
    if(b->HandleProtocol(h[i],&PathGuid,(VOID **)&p)==EFI_SUCCESS&&Inspect(p,&bytes,&usb,&windows)&&windows&&!usb) {
      matches++;volume=h[i];base=p;path_bytes=bytes;
    }
  }
  if(h)b->FreePool(h);
  if(s!=EFI_SUCCESS||matches!=1||volume==OwnVolume)return EFI_NOT_FOUND;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL;
  s=b->HandleProtocol(volume,&FsGuid,(VOID **)&fs);
  if(s==EFI_SUCCESS&&fs)s=fs->OpenVolume(fs,&root);else s=EFI_UNSUPPORTED;
  if(s!=EFI_SUCCESS||!root)return EFI_DEVICE_ERROR;
  VOID *bytes=NULL;s=ReadFile(root,WindowsName,3086680,&bytes);
  EFI_STATUS closed=root->Close(root);if(s==EFI_SUCCESS)s=closed;
  if(s==EFI_SUCCESS&&!GuHashMatches(bytes,3086680,"2d60a2203865122dad22772136b3d905167d9dcf926d602bb5f48b3b6668abec"))s=EFI_CRC_ERROR;
  UINTN node_bytes=4+sizeof(WindowsName),total=path_bytes-4+node_bytes+4;
  UINT8 *path=NULL;
  if(s==EFI_SUCCESS){path=GuAlloc(total);if(!path)s=EFI_OUT_OF_RESOURCES;}
  if(s==EFI_SUCCESS) {
    memcpy(path,base,path_bytes-4);UINT8 *node=path+path_bytes-4;
    node[0]=MEDIA_DEVICE_PATH;node[1]=MEDIA_FILEPATH_DP;node[2]=(UINT8)node_bytes;node[3]=(UINT8)(node_bytes>>8);
    memcpy(node+4,WindowsName,sizeof(WindowsName));node+=node_bytes;
    node[0]=END_DEVICE_PATH_TYPE;node[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;node[2]=4;node[3]=0;
    s=b->LoadImage(FALSE,Self,(VOID *)path,bytes,3086680,&Windows);
  }
  GuFree(bytes);GuFree(path);
  if(s==EFI_SUCCESS) {
    EFI_LOADED_IMAGE_PROTOCOL *im=NULL;s=b->HandleProtocol(Windows,&LoadedGuid,(VOID **)&im);
    if(s==EFI_SUCCESS&&(!im||im->DeviceHandle!=volume))s=EFI_UNSUPPORTED;
  }
  GuLog("Windows image staged: status=%#llx matches=%zu SHA256 pinned\n",(unsigned long long)s,(size_t)matches);
  return s;
}
EFI_STATUS GuBootChoose(BOOLEAN *unlock) {
  if(!unlock||!Table||!Table->ConIn||!Table->ConIn->ReadKeyStroke)return EFI_UNSUPPORTED;
  *unlock=FALSE;
  GuBootSay(L"\r\nGA102 EXPERIMENTAL v0.1.0 - validation in progress on this PC.\r\nU: one attempt, then Windows. S/Esc or 30 s: normal Windows.\r\nThe screen may stay FROZEN on this message while the GPU is released.\r\nDo not touch any peripherals. If Windows does not return after 4 minutes,\r\npower off completely, wait 10 s, then choose Windows Boot Manager.\r\nLog: USB GA102-UNLOCK/RUNxxxx.TXT. No automatic new attempt.\r\n");
  for(UINTN i=0;i<300;i++) {
    EFI_INPUT_KEY key;EFI_STATUS s=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(s==EFI_SUCCESS) {
      if(key.UnicodeChar=='u'||key.UnicodeChar=='U'){*unlock=TRUE;return EFI_SUCCESS;}
      if(key.ScanCode==0x17||key.UnicodeChar==27||key.UnicodeChar=='s'||key.UnicodeChar=='S')return EFI_SUCCESS;
    } else if(s!=EFI_NOT_READY)return s;
    s=Table->BootServices->Stall(100000);if(s!=EFI_SUCCESS)return s;
  }
  return EFI_SUCCESS;
}
EFI_STATUS GuBootClose(VOID) {
  EFI_STATUS s=EFI_SUCCESS;
  if(Log){s=Log->Flush(Log);EFI_STATUS closed=Log->Close(Log);Log=NULL;if(s==EFI_SUCCESS)s=closed;}
  if(Root){EFI_STATUS closed=Root->Close(Root);Root=NULL;if(s==EFI_SUCCESS)s=closed;}
  return s;
}
EFI_STATUS GuBootWindows(VOID) {
  if(!Windows)return EFI_NOT_READY;
  return Table->BootServices->StartImage(Windows,NULL,NULL);
}
VOID GuBootHalt(VOID) {
  GuBootSay(L"\r\nSTOPPED. Power OFF fully; next boot use Windows Boot Manager.\r\n");
  for(;;)__asm__ volatile("pause");
}
