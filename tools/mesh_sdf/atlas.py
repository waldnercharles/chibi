"""Shared deterministic Suzanne-compatible RG16 atlas encoding."""
import hashlib, math, struct, zlib
import numpy as np

def png(path,rgba):
 def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 h,w,_=rgba.shape;raw=b''.join(b'\0'+row.tobytes() for row in rgba)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

def validate_metadata(n, half, distance_range, lipschitz):
 """Mirror shader_volume_contract in src/graph/shader/shader_volume.c.
 Check float32 too: those are the values the native authoring path receives.
 """
 if not 2 <= n <= 144:
  raise ValueError('native sampled-SDF Resolution must be 2..144')
 half=np.asarray(half)
 if half.shape!=(3,) or not np.isfinite(half).all() or np.any(half<=1e-5) or np.any(half>1e6) or np.any(half.astype(np.float32)<=np.float32(1e-5)):
  raise ValueError('native sampled-SDF Half Size must be finite and in (1e-5, 1e6]; reduce padding/translation or rescale geometry')
 if not math.isfinite(distance_range) or not 0<distance_range<=1e6 or np.float32(distance_range)<=0:
  raise ValueError('native sampled-SDF Range must be finite and in (0, 1e6]; reduce padding or rescale geometry')
 if not math.isfinite(lipschitz) or not 1<=lipschitz<=16:
  raise ValueError('native sampled-SDF Lipschitz must be finite and in [1, 16]; rescale geometry or adjust grid resolution')

def encode(field, half, destination):
 n=field.shape[0]
 if field.shape!=(n,n,n) or not np.isfinite(field).all(): raise ValueError('sampled-SDF field must be a finite cubic lattice')
 distance_range=float(np.max(np.abs(field))*1.01)
 validate_metadata(n,half,distance_range,1)
 encoded=np.rint(np.clip((field/distance_range+1)*.5,0,1)*65535).astype(np.uint16)
 decoded=(encoded.astype(np.float32)*(np.float32(2)/np.float32(65535))-np.float32(1))*np.float32(distance_range)
 gradient=[float(np.max(np.abs(np.diff(decoded,axis=2-axis))))/(float(half[axis])*2/(n-1)) for axis in range(3)]
 lipschitz=max(1.,float(math.sqrt(sum(g*g for g in gradient))*1.0001+1e-5))
 validate_metadata(n,half,distance_range,lipschitz)
 border=np.concatenate([decoded[0].ravel(),decoded[-1].ravel(),decoded[:,0].ravel(),decoded[:,-1].ravel(),decoded[:,:,0].ravel(),decoded[:,:,-1].ravel()])
 if not np.isfinite(decoded).all() or border.min()<=0: raise ValueError('RG16 border must be finite and strictly positive; increase padding or rescale geometry')
 cols=math.ceil(math.sqrt(n));rows=math.ceil(n/cols);atlas=np.zeros((rows*n,cols*n,4),dtype=np.uint8);atlas[:,:,3]=255
 for iz in range(n):
  tile=atlas[(iz//cols)*n:(iz//cols+1)*n,(iz%cols)*n:(iz%cols+1)*n];tile[:,:,0]=(encoded[iz]>>8).astype(np.uint8);tile[:,:,1]=(encoded[iz]&255).astype(np.uint8)
 digest=14695981039346656037
 for b in atlas.tobytes(): digest=((digest^b)*1099511628211)&((1<<64)-1)
 name=f'{digest:016x}.sdf.png';path=destination/name;png(path,atlas)
 result={'resolution':n,'image':'content/sdf/'+name,'range':distance_range,'lipschitz':lipschitz,'spacing':(half*2/(n-1)).tolist(),'quantization_error_max':float(np.max(np.abs(decoded-field))),'border_min':float(border.min()),'pixels_fnv1a64':f'{digest:016x}','png_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'sample_bytes_sha256':hashlib.sha256(encoded.astype('<u2').tobytes()).hexdigest(),'atlas_size':[cols*n,rows*n]}
 return result,decoded
