#!/usr/bin/env python3
"""One-shot, metadata-only firmware timing capture. Close the live viewer first."""
import argparse
import fcntl
import glob
import json
import os
from pathlib import Path
import struct
import termios
import time
import tty

NAMES=('authentication','password_pbkdf2','password_kmac','storage_activation','usb_attach_marker',
       'usb_task','input_update','display_loop_pass','debug_task','usb_service_gap','input_service_gap','display_service_gap','display_present')
WORDS=8+len(NAMES)*6+65+128*5

def decode(payload):
    if len(payload)!=WORDS*4:
        raise ValueError('Unsupported diagnostics length')
    w=struct.unpack('<'+'I'*WORDS,payload)
    if w[0]!=1 or w[7]!=len(NAMES) or w[5]>128:
        raise ValueError('Unsupported diagnostics version or record count')
    result=dict(version=w[0],uptime_ms=w[1],read_requests=w[2],write_requests=w[3],
                partial_requests=w[4],overwritten_runs=w[6],timings={},read_regions=[],recent_runs=[])
    p=8
    for name in NAMES:
        count,lo,hi,maximum,first,last=w[p:p+6];p+=6
        total=lo+(hi<<32)
        result['timings'][name]=dict(count=count,total_us=total,mean_ms=total/count/1000 if count else 0,
                                    max_ms=maximum/1000,first_start_ms=first,last_end_ms=last)
    for i,count in enumerate(w[p:p+65]):
        if count: result['read_regions'].append(dict(first_lba=i*1024,last_lba=i*1024+1023 if i<64 else None,requests=count))
    p+=65
    for _ in range(w[5]):
        lba,count,first,last,write=w[p:p+5];p+=5
        result['recent_runs'].append(dict(lba=lba,requests=count,first_ms=first,last_ms=last,operation='write' if write&1 else 'read',partial=bool(write&2)))
    return result

def find_port():
    for name in glob.glob('/sys/class/tty/ttyACM*'):
        for parent in (Path(name)/'device').resolve().parents:
            try:
                if (parent/'idVendor').read_text().strip()=='cafe' and (parent/'idProduct').read_text().strip()=='4013':
                    return '/dev/'+Path(name).name
            except OSError: pass
    raise RuntimeError('No Fuse Vault debug serial port found')

def capture(port,reset=False):
    fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
    try:
        try: fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: raise RuntimeError('Close the USB screen viewer before capturing')
        tty.setraw(fd)
        fcntl.ioctl(fd,termios.TIOCMBIS,struct.pack('I',termios.TIOCM_DTR))
        termios.tcflush(fd,termios.TCIFLUSH)
        buffer=bytearray();deadline=time.monotonic()+45;next_request=0
        while time.monotonic()<deadline:
            now=time.monotonic()
            if now>=next_request:
                os.write(fd,b'k' if reset else b'j');reset=False;next_request=now+5
            try: buffer.extend(os.read(fd,65536))
            except BlockingIOError: pass
            start=buffer.find(b'FVT1')
            if start>=0 and len(buffer)>=start+32:
                size=struct.unpack_from('<I',buffer,start+4)[0]
                if size!=WORDS*4: raise RuntimeError('Unexpected diagnostic firmware version')
                if len(buffer)>=start+32+size:return decode(buffer[start+32:start+32+size])
            if len(buffer)>65536:del buffer[:-32768]
            time.sleep(.01)
        raise RuntimeError('No diagnostic reply in 45 seconds; check the diagnostics UF2 is flashed')
    finally: os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--label',default='')
    parser.add_argument('--reset',action='store_true',help='Clear only diagnostic counters before this capture')
    args=parser.parse_args()
    try:
        data=capture(args.port or find_port(),args.reset);data['label']=args.label
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(data,indent=2)+'\n')
    except (OSError,RuntimeError,ValueError) as exc:parser.exit(1,str(exc)+'\n')
    print(f"Saved {args.output}: {data['read_requests']} reads, {data['write_requests']} writes")
    for name,t in data['timings'].items():
        print(f"{name}: {t['count']} calls, total {t['total_us']/1e6:.3f}s, max {t['max_ms']:.3f}ms")
if __name__=='__main__':main()
