#include "host/workspace_files_web.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(void, workspace_files_web_js_init, (), {
    if(Module.browserWorkspaceFiles)return;
    const pathSafe = path => typeof path==='string' && path && path.length < 4096 && [...path].every(character => { const code=character.charCodeAt(0);return code>31&&code!==127&&code!==92; }) && !path.startsWith('/') && path.split('/').length <= 16 && path.split('/').every(p => p && p !== '.' && p !== '..');
    const crcTable=new Uint32Array(256);for(let i=0;i<256;i++){let c=i;for(let j=0;j<8;j++)c=(c>>>1)^((c&1)?0xedb88320:0);crcTable[i]=c;}
    const crc32 = bytes => { let crc=0xffffffff; for(const byte of bytes)crc=(crc>>>8)^crcTable[(crc^byte)&255];return (~crc)>>>0; };
    const zip = files => {
        if(!Array.isArray(files)||files.length>4096)throw Error('ZIP file count exceeds limit');
        const parts=[], directory=[];let offset=0,total=0,names=0;const unique=new Set();
        const header=(size,fields)=>{const bytes=new Uint8Array(size),view=new DataView(bytes.buffer);for(const [at,n,value] of fields){if(n===2)view.setUint16(at,value,true);else view.setUint32(at,value,true);}return bytes;};
        for(const [name,data] of files){
            const path=new TextEncoder().encode(name);names+=path.length;total+=data.length;
            if(!pathSafe(name)||unique.has(name)||!(data instanceof Uint8Array)||data.length>16777216||total>67108864||names>1048576)throw Error('ZIP path/file/byte budget exceeded');unique.add(name);const crc=crc32(data);
            const shared=[[6,2,0x800],[14,4,crc],[18,4,data.length],[22,4,data.length],[26,2,path.length]];
            const local=header(30,[[0,4,0x04034b50],[4,2,20],...shared]);
            const central=header(46,[[0,4,0x02014b50],[4,2,20],[6,2,20],...shared.map(([at,n,v])=>[at+2,n,v]),[42,4,offset]]);
            parts.push(local,path,data);directory.push(central,path);offset+=30+path.length+data.length;
        }
        const size=directory.reduce((sum,p)=>sum+p.length,0),end=header(22,[[0,4,0x06054b50],[8,2,files.length],[10,2,files.length],[12,4,size],[16,4,offset]]);
        return new Blob([...parts,...directory,end],{type:'application/zip'});
    };
    const unzip = async (buffer,isCancelled=()=>false) => {
        if(buffer.byteLength<22||buffer.byteLength>100663296)throw Error('ZIP input exceeds supported byte budget');
        const view=new DataView(buffer),files=[];let end=buffer.byteLength-22,total=0,nameBytes=0;
        while(end>=Math.max(0,buffer.byteLength-65557) && view.getUint32(end,true)!==0x06054b50)end--;
        if(end<0 || view.getUint32(end,true)!==0x06054b50)throw Error('Not a supported ZIP archive');
        const count=view.getUint16(end+10,true);if(count>4096)throw Error('ZIP entry count exceeds limit');
        let at=view.getUint32(end+16,true);
        const names=new Set();
        for(let i=0;i<count;i++){
            if(at+46>buffer.byteLength || view.getUint32(at,true)!==0x02014b50)throw Error('Invalid ZIP directory');
            const flags=view.getUint16(at+8,true),method=view.getUint16(at+10,true),compressed=view.getUint32(at+20,true),size=view.getUint32(at+24,true),length=view.getUint16(at+28,true),local=view.getUint32(at+42,true),crc=view.getUint32(at+16,true);
            if(at+46+length>buffer.byteLength)throw Error('Invalid ZIP name');
            const name=new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array(buffer,at+46,length));nameBytes+=length;if(nameBytes>1048576)throw Error('ZIP name budget exceeded');
            at+=46+length+view.getUint16(at+30,true)+view.getUint16(at+32,true);
            if(name.endsWith('/'))continue;
            if(!pathSafe(name)||names.has(name)||flags&1||size>16777216||total+size>67108864||(method!==0&&method!==8))throw Error('ZIP path, format or byte limit refused');
            names.add(name);total+=size;
            if(local+30>buffer.byteLength||view.getUint32(local,true)!==0x04034b50)throw Error('Invalid ZIP local entry');
            const start=local+30+view.getUint16(local+26,true)+view.getUint16(local+28,true);if(start+compressed>buffer.byteLength)throw Error('Truncated ZIP payload');
            const input=new Uint8Array(buffer,start,compressed);
            let data=input;
            if(method===8){
                const reader=new Blob([input]).stream().pipeThrough(new DecompressionStream('deflate-raw')).getReader(),chunks=[];let decoded=0;
                for(;;){if(isCancelled()){await reader.cancel();throw Error('Archive operation cancelled');}const chunk=await reader.read();if(chunk.done)break;decoded+=chunk.value.length;if(decoded>size||decoded>16777216){await reader.cancel();throw Error('Inflated ZIP entry exceeds declared limit');}chunks.push(chunk.value);}
                data=new Uint8Array(decoded);let offset=0;for(const chunk of chunks){data.set(chunk,offset);offset+=chunk.length;}
            }
            if(data.length!==size || crc32(data)!==crc)throw Error('ZIP size or checksum mismatch');
            files.push([name,data]);if(isCancelled())throw Error('Archive operation cancelled');
        }
        return files;
    };
    const compare=(a,b)=>{const left=new TextEncoder().encode(a),right=new TextEncoder().encode(b);for(let i=0;i<Math.min(left.length,right.length);i++)if(left[i]!==right[i])return left[i]-right[i];return left.length-right.length;};
    const utf8=new TextEncoder();
    const validPath=path=>pathSafe(path)&&new TextDecoder('utf-8',{fatal:true}).decode(utf8.encode(path))===path;
    const captureBytes=(root,overlays=[])=>{
        if(typeof root!=='string'||!root.startsWith('/')||!FS.isDir(FS.lstat(root).mode))throw Error('Archive root must be a real directory');
        if(!Array.isArray(overlays)||overlays.length>4096)throw Error('Overlay count exceeds limit');
        const replacements=new Map();
        for(const overlay of overlays){
            if(!overlay||!validPath(overlay.path)||typeof overlay.text!=='string'||replacements.has(overlay.path))throw Error('Invalid or duplicated text overlay');
            const bytes=utf8.encode(overlay.text);if(bytes.length>1048576)throw Error('Text overlay exceeds 1 MiB');replacements.set(overlay.path,bytes);
        }
        const result=new Map();let total=0,entries=0,nameBytes=0;
        const add=(name,data)=>{
            nameBytes+=utf8.encode(name).length;
            if(nameBytes>1048576||!validPath(name)||result.size>=4096||data.length>16777216||total+data.length>67108864)throw Error('Archive path/file/byte limit exceeded');
            result.set(name,data);total+=data.length;
        };
        const walk=(directory,relative,depth)=>{
            if(depth>16)throw Error('Archive depth exceeds limit');
            const names=FS.readdir(directory);if(names.length>4098)throw Error('Archive directory entry budget exceeded');
            for(const name of names.filter(n=>n!=='.'&&n!=='..'&&n!=='.chibi').sort(compare)){
                if(++entries>4096)throw Error('Archive entry budget exceeded');
                const path=directory+'/'+name,key=relative?relative+'/'+name:name,stat=FS.lstat(path);
                if(FS.isDir(stat.mode))walk(path,key,depth+1);
                else if(!FS.isFile(stat.mode))throw Error('Archive excludes symbolic links and special files');
                else {
                    if(stat.size>16777216)throw Error('Archive file exceeds 16 MiB');
                    const bytes=replacements.has(key)?replacements.get(key):FS.readFile(path);
                    replacements.delete(key);add(key,bytes);
                }
            }
        };
        walk(root,"",0);
        for(const [name,data]of replacements)add(name,data);
        return [...result].sort((a,b)=>compare(a[0],b[0]));
    };
    let nextRoot=0;
    const ownedRoots=new Set(),adoptedRoots=new Set(),rootAttachments=new Map();
    let attachmentBytes=0;
    const releaseAttachments=root=>{const records=rootAttachments.get(root);if(!records)return;for(const bytes of records.values())attachmentBytes-=bytes.length;rootAttachments.delete(root);};
    const attachment=(root,name)=>{const bytes=rootAttachments.get(root)?.get(name);return bytes?bytes.slice():null;};
    const remove=root=>{
        const visit=path=>{if(FS.isDir(FS.lstat(path).mode)){for(const name of FS.readdir(path))if(name!=='.'&&name!=='..')visit(path+'/'+name);FS.rmdir(path);}else FS.unlink(path);};
        try{visit(root);}catch{}
    };
    const installBytes=(files,isCancelled=()=>false)=>{
        if(!Array.isArray(files)||files.length>4096)throw Error('Archive file count exceeds limit');
        if(ownedRoots.size>=8)throw Error('Pending workspace installation limit exceeded');
        const names=new Set(),copied=[];let total=0,nameBytes=0;
        for(const pair of files){
            if(!Array.isArray(pair)||pair.length!==2||!validPath(pair[0])||names.has(pair[0])||!(pair[1]instanceof Uint8Array)||pair[1].length>16777216||total+pair[1].length>67108864)throw Error('Invalid archive file/path/byte budget');
            names.add(pair[0]);nameBytes+=utf8.encode(pair[0]).length;if(nameBytes>1048576)throw Error('Archive path budget exceeded');total+=pair[1].length;copied.push(pair);
        }
        if(isCancelled())throw Error('Archive operation cancelled');
        if(!FS.analyzePath('/tmp',true).exists)FS.mkdir('/tmp');
        if(!FS.isDir(FS.lstat('/tmp').mode))throw Error('Temporary root must be a real directory');
        let root="";
        for(let i=0;i<64;i++){const candidate='/tmp/chibi-workspace-'+(++nextRoot);if(!FS.analyzePath(candidate,true).exists){root=candidate;break;}}
        if(!root)throw Error('Fresh workspace identity budget exhausted');
        FS.mkdir(root,448);
        try{
            for(const [name,data]of copied){const path=root+'/'+name;FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));FS.writeFile(path,data,{flags:'wx'});}
        }catch(error){remove(root);throw error;}
        ownedRoots.add(root);
        return root;
    };
    const discard=root=>{if(!ownedRoots.has(root))throw Error('Root is not an owned unadopted installation');remove(root);ownedRoots.delete(root);releaseAttachments(root);};
    const within=(root,owned)=>typeof root==='string'&&(root===owned||root.startsWith(owned+'/'));
    const adopt=root=>{for(const owned of ownedRoots)if(within(root,owned)){ownedRoots.delete(owned);adoptedRoots.add(owned);releaseAttachments(owned);}};
    const release=(root,retained)=>{
        for(const owned of adoptedRoots){
            if(!within(root,owned))continue;
            if(within(retained,owned))return false;
            remove(owned);
            if(FS.analyzePath(owned,true).exists)return false;
            adoptedRoots.delete(owned);releaseAttachments(owned);return true;
        }
        return false;
    };
    const digest=async bytes=>{
        if(!globalThis.crypto?.subtle)throw Error('SHA-256 requires a secure browser context');
        return [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(v=>v.toString(16).padStart(2,'0')).join("");
    };
    const encode=bytes=>{let binary="";for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(binary);};
    const decode=(base64,limit=16777216)=>{
        if(typeof base64!=='string'||base64.length>4*Math.ceil(limit/3)||base64.length%4||!/^[A-Za-z0-9+/]*={0,2}$/.test(base64))throw Error('Invalid canonical base64');
        const binary=atob(base64);if(binary.length>limit||btoa(binary)!==base64)throw Error('Invalid canonical base64');
        const bytes=new Uint8Array(binary.length);for(let i=0;i<bytes.length;i++)bytes[i]=binary.charCodeAt(i);return bytes;
    };
    const attachmentName=name=>typeof name==='string'&&/^[A-Za-z0-9_-]{1,64}$/.test(name);
    const canonicalArchive=(files,attachments)=>JSON.stringify(attachments===undefined?files:{files,attachments});
    const capture=async(root,overlays=[],inputAttachments)=>{
        const source=captureBytes(root,overlays),files=[];let bytes=0,estimate=0;
        const attachments=inputAttachments===undefined?undefined:[];
        if(inputAttachments!==undefined&&(!Array.isArray(inputAttachments)||inputAttachments.length>8))throw Error('Archive attachment count exceeds limit');
        const staged=[];let last=null;
        for(const item of inputAttachments||[]){
            if(!item||!attachmentName(item.name)||(last!==null&&compare(last,item.name)>=0)||!(item.data instanceof Uint8Array)||item.data.length>33554432)throw Error('Invalid archive attachment');
            last=item.name;const data=item.data.slice();staged.push({name:item.name,data});
            bytes+=data.length;estimate+=4*Math.ceil(data.length/3)+item.name.length+160;
        }
        estimate+=source.reduce((n,[name,data])=>n+4*Math.ceil(data.length/3)+utf8.encode(name).length+128,0);
        if(estimate>33554432)throw Error('Serialized archive exceeds 32 MiB');
        for(const [path,data]of source){bytes+=data.length;files.push({path,base64:encode(data),sha256:await digest(data)});}
        for(const item of staged)attachments.push({name:item.name,base64:encode(item.data),bytes:item.data.length,sha256:await digest(item.data)});
        const canonical=canonicalArchive(files,attachments);if(utf8.encode(canonical).length>33554200||bytes>67108864)throw Error('Serialized archive exceeds limit');
        const result={files,bytes,sha256:await digest(utf8.encode(canonical))};
        if(attachments!==undefined)result.attachments=attachments;
        return result;
    };
    const install=async archive=>{
        if(!archive||!Array.isArray(archive.files)||archive.files.length>4096||!Number.isSafeInteger(archive.bytes)||archive.bytes<0||archive.bytes>67108864||typeof archive.sha256!=='string'||!/^[0-9a-f]{64}$/.test(archive.sha256))throw Error('Invalid archive metadata');
        if(archive.attachments!==undefined&&(!Array.isArray(archive.attachments)||archive.attachments.length>8))throw Error('Invalid archive attachments');
        const expectedDigest=archive.sha256;
        const staged=[],canonical=[],metadata=new Map(),attachmentRecords=archive.attachments===undefined?undefined:[];
        let total=0,last=null,metadataBytes=0;
        for(const item of archive.files){
            if(!item||!validPath(item.path)||(last!==null&&compare(last,item.path)>=0)||typeof item.sha256!=='string'||!/^[0-9a-f]{64}$/.test(item.sha256))throw Error('Archive paths/digests must be unique and canonical');
            last=item.path;const data=decode(item.base64);total+=data.length;if(total>67108864)throw Error('Archive exceeds decoded byte budget');
            staged.push([item.path,data]);canonical.push({path:item.path,base64:item.base64,sha256:item.sha256});
        }
        last=null;
        for(const item of archive.attachments||[]){
            if(!item||!attachmentName(item.name)||(last!==null&&compare(last,item.name)>=0)||!Number.isSafeInteger(item.bytes)||item.bytes<0||item.bytes>33554432||typeof item.sha256!=='string'||!/^[0-9a-f]{64}$/.test(item.sha256))throw Error('Invalid archive attachment metadata');
            last=item.name;const data=decode(item.base64,33554432);
            if(data.length!==item.bytes)throw Error('Archive attachment byte mismatch');
            total+=data.length;metadataBytes+=data.length;
            if(total>67108864||metadataBytes+attachmentBytes>67108864)throw Error('Retained archive attachment budget exceeded');
            metadata.set(item.name,data);attachmentRecords.push({name:item.name,base64:item.base64,bytes:item.bytes,sha256:item.sha256});
        }
        const encoded=utf8.encode(canonicalArchive(canonical,attachmentRecords));if(encoded.length>33554200||total!==archive.bytes)throw Error('Archive byte count/envelope mismatch');
        if(await digest(encoded)!==expectedDigest)throw Error('Archive digest mismatch');
        for(let i=0;i<staged.length;i++)if(await digest(staged[i][1])!==canonical[i].sha256)throw Error('Archive file digest mismatch');
        for(const item of attachmentRecords||[])if(await digest(metadata.get(item.name))!==item.sha256)throw Error('Archive attachment digest mismatch');
        // Recheck after awaits: another install may have consumed metadata capacity.
        if(metadataBytes+attachmentBytes>67108864)throw Error('Retained archive attachment budget exceeded');
        const root=installBytes(staged);
        if(metadata.size){rootAttachments.set(root,metadata);attachmentBytes+=metadataBytes;}
        return root;
    };
    Module.browserWorkspaceFiles=Object.freeze({pathSafe:validPath,captureBytes,installBytes,zip,unzip,capture,install,attachment,remove:discard,adopt,release});
});
EM_JS(void, workspace_files_web_js_adopt, (const char *root), {
    Module.browserWorkspaceFiles?.adopt(UTF8ToString(root));
});
EM_JS(bool, workspace_files_web_js_release, (const char *root,const char *retained), {
    return Module.browserWorkspaceFiles?.release(root?UTF8ToString(root):null,retained?UTF8ToString(retained):null)?1:0;
});
EM_JS(unsigned char *, workspace_files_web_js_document_state, (const char *root,size_t *size), {
    HEAPU32[size>>>2]=0;
    const bytes=Module.browserWorkspaceFiles?.attachment(UTF8ToString(root),'workspace-documents-v1');
    if(!bytes)return 0;
    const pointer=_malloc(bytes.length||1);
    if(!pointer)throw Error('Document snapshot allocation failed');
    HEAPU8.set(bytes,pointer);HEAPU32[size>>>2]=bytes.length;
    return pointer;
});
// clang-format on
/* Real C definitions let the linker extract these helpers from a static archive. */
void workspace_files_web_init()
{
    workspace_files_web_js_init();
}
void workspace_files_web_adopt(const char *root)
{
    workspace_files_web_js_adopt(root);
}
bool workspace_files_web_release(const char *retired, const char *retained)
{
    return workspace_files_web_js_release(retired, retained);
}
unsigned char *workspace_files_web_document_state(const char *root, size_t *bytes)
{
    return workspace_files_web_js_document_state(root, bytes);
}
#else
void workspace_files_web_init() {}
void workspace_files_web_adopt(const char *) {}
bool workspace_files_web_release(const char *, const char *)
{
    return false;
}
unsigned char *workspace_files_web_document_state(const char *, size_t *size)
{
    *size = 0;
    return NULL;
}
#endif
