#!/usr/bin/env python3
"""Matched B16 (Gate 8C) vs L0B16 (Gate 8D) comparison. Each trace is symbolised with its OWN executable; raw PCs are never compared.
Counts of unique lines / I-cache indices are footprints, NOT miss counts."""
import struct,re,bisect,collections,sys,json
EV='/home/gpf/code/SuperMarioAdvance2Recomp/docs/results/evidence/sma2_low_perturbation_pmu_20261017/elf/'
CFG={'B16':dict(trace='/tmp/nulldc3-sh4trace-gate8c-20261017/b16_1m.n3it',elf='/tmp/nulldc3-sh4trace-gate8c-20261017/B16.elf',nm='/tmp/nulldc3-sh4trace-gate8c-20261017/B16_nm_sizes.txt',map=EV+'B16-LOWPMU-base.map',retire=0x8C01BEB0,bridge=0x8C052874,decode=0x8C077148,step=0x8C077CA0,irqv=0x8C0E6450),
     'L0B16':dict(trace='/tmp/nulldc3-sh4trace-gate8d-20261017/l0b16_1m.n3it',elf='/tmp/nulldc3-sh4trace-gate8d-20261017/L0B16.elf',nm='/tmp/nulldc3-sh4trace-gate8d-20261017/L0B16_nm_sizes.txt',map=EV+'L0B16-LOWPMU-base.map',retire=0x8C01FE58,bridge=0x8C055120,decode=0x8C07A084,step=0x8C07ABDC,irqv=0x8C0E999C)}
def norm(n):
    n=re.sub(r'\s*\[clone [^\]]*\]','',n); n=re.sub(r'\.(lto_priv|constprop|isra|part|cold)\.\d+','',n); n=re.sub(r'\.(constprop|isra|part|cold|lto_priv)\b','',n)
    return n.strip()
def load_syms(p):
    s=[]
    for l in open(p):
        q=l.rstrip('\n').split(' ',3)
        if len(q)==4 and q[2] in 'TtWw':
            try: s.append((int(q[0],16),int(q[1],16),norm(q[3])))
            except: pass
    s.sort(); return s
def load_map(p):
    """text input sections: (addr,size,file)"""
    out=[];lines=open(p,errors='replace').read().split('\n');i=0
    pat1=re.compile(r'^ (\.text\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$'); pat0=re.compile(r'^ (\.text\S*)$'); pat2=re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
    while i<len(lines):
        m=pat1.match(lines[i])
        if m and int(m.group(3),16)>0 and int(m.group(2),16)>=0x8c000000: out.append((int(m.group(2),16),int(m.group(3),16),m.group(4)))
        elif pat0.match(lines[i]) and i+1<len(lines):
            m2=pat2.match(lines[i+1])
            if m2 and int(m2.group(2),16)>0 and int(m2.group(1),16)>=0x8c000000: out.append((int(m2.group(1),16),int(m2.group(2),16),m2.group(3))); 
        i+=1
    out.sort(); return out
def cls_file(f):
    if 'libkallisti' in f or 'libkosext2fs' in f or 'libkosutils' in f or '/kos/' in f or 'crt' in f.lower() and 'ltrans' not in f: return 'KOS'
    if re.search(r'libc\.a|libm\.a|libgcc|libstdc\+\+|libg\.a|libnosys|libsupc',f): return 'toolchain libc/libgcc/libstdc++'
    if 'ltrans' in f or f.endswith('.o'): return 'application (LTO)'
    return 'other:'+f[:40]
def load(c):
    d=open(c['trace'],'rb').read(); n=struct.unpack_from('<I',d,16)[0]
    R=[struct.unpack_from('<BBHQI',d,24+16*i) for i in range(n)]; ins=R[1:-1]
    E=open(c['elf'],'rb').read(); phoff,=struct.unpack_from('<I',E,28); ph,=struct.unpack_from('<H',E,44)
    segs=[s for s in (struct.unpack_from('<8I',E,phoff+32*i) for i in range(ph)) if s[0]==1]
    inelf=lambda a: any(va<=a<va+fs for t,off,va,pa,fs,ms,fl,al in segs)
    sy=load_syms(c['nm']); K=[s[0] for s in sy]; mp=load_map(c['map']); MK=[m[0] for m in mp]
    return ins,inelf,sy,K,mp,MK
def analyse(name,c,upto=None):
    ins,inelf,sy,K,mp,MK=load(c)
    if upto is None: upto=len(ins)
    ins=ins[:upto]; pcs=[r[4] for r in ins]
    def sym(a):
        i=bisect.bisect_right(K,a)-1
        return sy[i][2] if i>=0 and a<sy[i][0]+max(sy[i][1],2) else ('(label) at map section' )
    def origin(a):
        if not inelf(a): return 'REIOS/HLE trap (non-ELF)'
        i=bisect.bisect_right(MK,a)-1
        if i>=0 and a<mp[i][0]+mp[i][1]: return cls_file(mp[i][2])
        return 'unmapped ELF text'
    elfp=[p for p in pcs if inelf(p)]
    r=dict(name=name,instructions=len(pcs),
      retire_impl=sum(p==c['retire'] for p in pcs),bridge=sum(p==c['bridge'] for p in pcs),decode=sum(p==c['decode'] for p in pcs),step=sum(p==c['step'] for p in pcs),
      irq_entries=sum(p==c['irqv'] for p in pcs),traps=sum(1 for x in ins if x[2]==0x085B),
      unique_pcs=len(set(elfp)),unique_32B_lines=len({p>>5 for p in elfp}),unique_8KiB_dm_indices=len({(p>>5)&0xFF for p in elfp}))
    fn=collections.Counter(); org=collections.Counter()
    for p in pcs:
        o=origin(p); org[o]+=1
        fn[sym(p) if inelf(p) else 'REIOS trap (non-ELF)']+=1
        if o=='KOS' and False: pass
    r['functions']=fn; r['origin']=org
    # KOS functions by name
    return r
def retire_idx(c):
    ins=load(c)[0]; return [i for i,x in enumerate(ins) if x[4]==c['retire']]
res={}
full={k:analyse(k,v) for k,v in CFG.items()}
ri={k:retire_idx(v) for k,v in CFG.items()}
K_=min(len(ri['B16']),len(ri['L0B16']))
cut={k:ri[k][K_-1] for k in ri}   # SH-4 index of the K-th retire_impl entry in each trace (exclusive prefix end)
pre={k:analyse(k,CFG[k],cut[k]) for k in CFG}
def show(title,A,B):
    print('\n==',title,'=='); print('%-34s %12s %12s'%('metric','B16','L0B16'))
    for m in ('instructions','retire_impl','decode','step','bridge','irq_entries','traps','unique_pcs','unique_32B_lines','unique_8KiB_dm_indices'):
        print('%-34s %12d %12d'%(m,A[m],B[m]))
    print('execution origin (share of instructions):')
    for o in sorted(set(A['origin'])|set(B['origin'])):
        print('  %-40s %7.2f%% %7.2f%%   (%d / %d)'%(o,100*A['origin'][o]/A['instructions'],100*B['origin'][o]/B['instructions'],A['origin'][o],B['origin'][o]))
    print('top functions by instruction share (normalised names, each from its own symbol table):')
    names=[n for n,_ in (A['functions']+B['functions']).most_common(30)]
    for n in names: print('  %6.2f%% %6.2f%%  %s'%(100*A['functions'][n]/A['instructions'],100*B['functions'][n]/B['instructions'],n[:95]))
    return
show('FULL 1,000,000-instruction traces',full['B16'],full['L0B16'])
print('\ncommon comparable prefix: guest instructions up to retire_impl entry #%d in both (B16 sample has %d entries, L0B16 %d); SH-4 index cut B16 %d, L0B16 %d'%(K_,len(ri['B16']),len(ri['L0B16']),cut['B16'],cut['L0B16']))
show('COMMON GUEST PREFIX (%d guest retire entries)'%K_,pre['B16'],pre['L0B16'])
json.dump({k:{m:v for m,v in a.items() if m not in('functions','origin')}|{'origin':dict(a['origin']),'functions_top50':a['functions'].most_common(50)} for k,a in {'full_B16':full['B16'],'full_L0B16':full['L0B16'],'prefix_B16':pre['B16'],'prefix_L0B16':pre['L0B16']}.items()},open('comparison.json','w'),indent=1)
