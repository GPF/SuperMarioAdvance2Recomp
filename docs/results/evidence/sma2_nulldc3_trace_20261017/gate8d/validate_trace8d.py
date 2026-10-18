import struct,sys,re,bisect,collections
T,ELF,NM=sys.argv[1:4]; d=open(T,'rb').read(); ok=True
def chk(n,c,x=''):
    global ok; ok&=bool(c); print(('PASS ' if c else 'FAIL ')+n+(' - '+str(x) if x!='' else ''))
m,v,hs,st,mode,_,lim,cnt,drop=struct.unpack_from('<4sHHBBHIII',d,0)
chk('magic/version/header',m==b'N3IT' and v==1 and hs==24,(m,v,hs))
chk('status complete(2), interpreter(1), limit 1000000, count 1000002, dropped 0',(st,mode,lim,cnt,drop)==(2,1,1000000,1000002,0),(st,mode,lim,cnt,drop))
chk('file size == 24+16*count',len(d)==24+16*cnt,len(d))
R=[struct.unpack_from('<BBHQI',d,24+16*i) for i in range(cnt)]
types=collections.Counter(r[0] for r in R); chk('record types: 1 start, 1000000 instruction, 1 end, no overflow/backend-exit',dict(types)=={1:1,2:1000000,3:1},dict(types))
ins=R[1:-1]; chk('start seq 0, instruction seq 1..1000000 contiguous, end seq 1000001 opcode 0',R[0][3]==0 and [r[3] for r in ins]==list(range(1,1000001)) and R[-1][3]==1000001 and R[-1][2]==0)
chk('mode 1 on every record',all(r[1]==1 for r in R))
E=open(ELF,'rb').read(); phoff,=struct.unpack_from('<I',E,28); n,=struct.unpack_from('<H',E,44)
segs=[struct.unpack_from('<8I',E,phoff+32*i) for i in range(n)]; segs=[s for s in segs if s[0]==1]
def region(a):
    for t,off,va,pa,fs,ms,fl,al in segs:
        if va<=a<va+fs: return 'elf-file'
        if va<=a<va+ms: return 'elf-bss'
    p=a&0x1FFFFFFF
    if a>>29 in (4,5) and p<0x00200000: return 'bios-rom'
    if a>>29 in (4,5) and 0x0C000000<=p<0x0D000000:
        return 'ram-dcload-reios' if p<0x0C010000 else 'ram-other'
    return 'other'
def rd16(a):
    for t,off,va,pa,fs,ms,fl,al in segs:
        if va<=a<va+fs-1: return struct.unpack_from('<H',E,off+a-va)[0]
first=ins[0]
chk('first PC 0x8C01FEA2, opcode 0xE750 (bytes 50e7) == ELF word',first[4]==0x8C01FEA2 and first[2]==0xE750 and rd16(0x8C01FEA2)==0xE750,(hex(first[4]),hex(first[2])))
reg=collections.Counter(region(r[4]) for r in ins); print('regions of executed PCs:',dict(reg))
bad=[];known=0
for r in ins:
    if region(r[4])=='elf-file':
        known+=1
        if rd16(r[4])!=r[2]: bad.append((hex(r[4]),hex(r[2]),hex(rd16(r[4]))))
chk('every ELF-file-backed PC: recorded opcode == ELF word',not bad,(known,'checked',len(bad),'mismatch',bad[:3]))
nonelf=collections.Counter(); 
for r in ins:
    g=region(r[4])
    if g!='elf-file': nonelf[(g,r[4]>>12)]+=1
print('non-ELF-file PCs by (region, 4K page):',dict(nonelf))
# control flow
def cls(op):
    if (op&0xF000)==0xA000 or (op&0xF000)==0xB000: return 'dbra'
    if (op&0xFF00) in (0x8D00,0x8F00): return 'dcond'
    if (op&0xFF00) in (0x8900,0x8B00): return 'cond'
    if (op&0xF0FF) in (0x0023,0x0003,0x402B,0x400B) or op in (0x000B,0x002B): return 'dind'
    if (op&0xFF00)==0xC300: return 'trapa'
    return 'plain'
def s8(x): return x-256 if x>=128 else x
def s12(x): return x-4096 if x>=2048 else x
IRQV=0x8C0E999C  # _vma_table_600 (KOS VBR+0x600 interrupt entry; nm shows it as a local label)
irq_events=[];irq_after_slot=[];trap_events=[];irq_after_dcond=[];disc=[];slot_ok=0;nd=0;tgt_checked=0;tgt_bad=[];bad_slot=[];i=0;N=len(ins)
while i<N-1:
    pc,op=ins[i][4],ins[i][2];c=cls(op);nx=ins[i+1][4]
    if c in('dbra','dcond','dind'):
        nd+=1
        if nx==IRQV and c=='dcond':
            # a bf/s/bt/s that is NOT taken has no executed slot: pc+2 is an ordinary instruction and an interrupt can enter first.
            irq_after_dcond.append(i); irq_events.append(i); i+=1; continue
        if nx!=pc+2: bad_slot.append((i,hex(pc),hex(nx))); i+=1; continue
        slot=ins[i+1]
        if cls(slot[2])!='plain': bad_slot.append((i,'slot is branch',hex(slot[4]))); 
        slot_ok+=1
        if i+2<N:
            t=ins[i+2][4]
            if c=='dbra':
                disp=s12(op&0xFFF) if (op&0xF000) in (0xA000,0xB000) else 0; exp=pc+4+disp*2; tgt_checked+=1
                if t==IRQV:
                    irq_events.append(i+2); irq_after_slot.append((i,exp))
                elif t!=exp: tgt_bad.append((i,hex(pc),hex(t),hex(exp)))
            elif c=='dcond':
                exp=pc+4+s8(op&0xFF)*2; tgt_checked+=1
                if t==IRQV:
                    irq_events.append(i+2); irq_after_slot.append((i,exp))
                elif t not in(exp,pc+4): tgt_bad.append((i,hex(pc),hex(t),hex(exp)))
        i+=2; continue
    if nx==IRQV: irq_events.append(i)
    elif op==0x085B and region(pc)=='ram-dcload-reios': trap_events.append(i)
    elif c=='cond':
        exp=pc+4+s8(op&0xFF)*2
        if nx not in(pc+2,exp): disc.append((i,hex(pc),hex(op),hex(nx)))
    elif c=='trapa': disc.append((i,hex(pc),hex(op),hex(nx)))
    elif nx!=pc+2: disc.append((i,hex(pc),hex(op),hex(nx)))
    i+=1
res_ok=True
for i in irq_after_dcond:
    pc=ins[i][4]
    j=next((j for j in range(i+2,N) if ins[j][4]==pc+2),None)
    res_ok&= j is not None
    print('irq after bf/s/bt/s at %08x (index %d): resumes at pc+2 (not taken) after %s records'%(pc,i,'?' if j is None else j-i))
chk('every interrupt entry after a delayed-conditional resumes at pc+2 (branch not taken, slot not executed)',res_ok,len(irq_after_dcond))
print('explained: async interrupt entries at _vma_table_600:',len(irq_events),'; REIOS HLE traps (opcode 0x085B in the dcload/REIOS RAM area, return to PR):',len(trap_events))
for i,exp in irq_after_slot:
    j=next((j for j in range(i+3,N) if ins[j][4]==exp),None)
    print('irq entered after the slot of the delayed branch at %08x (index %d): later resumes at its target %08x after %s records'%(ins[i][4],i,exp,'NEVER' if j is None else j-i))
chk('every interrupt entered after a delay slot later resumes at the branch target (taken-branch SPC)',all(any(ins[j][4]==exp for j in range(i+3,N)) for i,exp in irq_after_slot),len(irq_after_slot))
chk('delayed branches: slot executes at pc+2 and the slot is not a branch',not bad_slot,(nd,'delayed branches',len(bad_slot),'bad',bad_slot[:3]))
chk('direct delayed branch targets (bra/bsr, bt/s, bf/s) match their displacement',not tgt_bad,(tgt_checked,'checked',len(tgt_bad),'bad',tgt_bad[:3]))
chk('no unexplained discontinuities (non-branch not followed by pc+2; conditional not at pc+2/target; trapa)',not disc,(len(disc),disc[:5]))
print('trace tail: last pc',hex(ins[-1][4]),'(a delayed branch/slot split at the end is not checked)')
# symbols
syms=[]
for l in open(NM):
    p=l.rstrip('\n').split(' ',3)
    if len(p)==4 and p[2] in 'TtWw':
        try: syms.append((int(p[0],16),int(p[1],16),p[3]))
        except: pass
syms.sort(); K=[s[0] for s in syms]
def sym(a):
    i=bisect.bisect_right(K,a)-1
    return syms[i][2] if i>=0 and a<syms[i][0]+max(syms[i][1],2) else 'UNSYMBOLIZED %08x'%a
fn=collections.Counter(); 
for r in ins:
    fn[sym(r[4]) if region(r[4])=='elf-file' else 'NON-ELF '+region(r[4])]+=1
print('distinct functions',len(fn)); [print('%8d %5.2f%%  %s'%(c,c/10000,s[:110])) for s,c in fn.most_common(25)]
pcs=[r[4] for r in ins]
ENT={'retire_impl':0x8C01FE58,'runtime_bridge_interpret':0x8C055120,'ThumbDecoder::decode':0x8C07A084,'Interpreter::step.part.0':0x8C07ABDC,'runtime_tick':0x8C04171C,'dispatch_resolve':0x8C0839E0,'runtime_dispatch':0x8C084854,'capture_named':0x8C01FD30,'sma2pmu_get_ticks':0x8C10C300}
ent={k:sum(1 for p in pcs if p==a) for k,a in ENT.items()}
for k,v in ent.items(): print('entries to %-28s %08x : %d'%(k,ENT[k],v))
chk('intended path present: retire_impl and runtime_bridge_interpret both entered',ent['retire_impl']>0 and ent['runtime_bridge_interpret']>0)
chk('no window-end/capture code in the sample (capture_named never entered)',ent['capture_named']==0)
print('GUEST INSTRUCTIONS RETIRED IN SAMPLE (retire_impl entries; the first window instruction retires at index 0):',ent['retire_impl'])
print('RESULT','TRACE VALID' if ok else 'TRACE INVALID')
sys.exit(0 if ok else 1)
