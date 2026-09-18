import ctypes
lib = ctypes.CDLL(r'C:\Acer\Code\TezzNative\TezzNative-language\run\tzcpu.dll')
i64=ctypes.c_int64; f32=ctypes.c_float

def fn(name,args,res=i64):
    f=getattr(lib,name); f.argtypes=args; f.restype=res; return f

malloc=fn('tz_cpu_malloc',[i64]); zeros=fn('tz_cpu_zeros',[i64])
fill=fn('tz_cpu_fill_f32',[i64,i64,f32])
xavier=fn('tz_cpu_xavier_init_f32',[i64,i64,i64,i64])
gemm_nt=fn('tz_cpu_gemm_nt_f32',[i64,i64,i64,i64,i64,i64,f32,f32])
gemm=fn('tz_cpu_gemm_f32',[i64,i64,i64,i64,i64,i64,f32,f32])
bias=fn('tz_cpu_add_bias_f32',[i64,i64,i64,i64])
relu_f=fn('tz_cpu_relu_f32',[i64,i64,i64])
relu_b=fn('tz_cpu_relu_bwd_f32',[i64,i64,i64,i64])
ce_fwd=fn('tz_cpu_cross_entropy_f32',[i64,i64,i64,i64,i64])
ce_bwd=fn('tz_cpu_cross_entropy_bwd_f32',[i64,i64,i64,i64,i64])
reduce=fn('tz_cpu_reduce_sum_cols_f32',[i64,i64,i64,i64])
adam=fn('tz_cpu_adam_f32',[i64,i64,i64,i64,f32,f32,f32,f32,f32,f32,i64])
report=fn('tz_cpu_report_loss',[i64,i64,i64])

def rd(ptr,n):
    buf=(ctypes.c_float*n).from_address(ptr); return list(buf)

def wr(ptr,vals):
    buf=(ctypes.c_float*len(vals)).from_address(ptr)
    for i,v in enumerate(vals): buf[i]=v

def wi(ptr,vals):
    buf=(ctypes.c_int32*len(vals)).from_address(ptr)
    for i,v in enumerate(vals): buf[i]=v

# Full MNIST-scale dims
B,IN,H,C=64,784,256,10
WH,WO=H*IN,C*H

W1=malloc(WH*4); b1=zeros(H); W2=malloc(WO*4); b2=zeros(C)
X=malloc(B*IN*4); Y=malloc(B*4)
pre1=malloc(B*H*4); h1=malloc(B*H*4)
logits=malloc(B*C*4); loss_buf=zeros(B)
dlogits=malloc(B*C*4)
dW2=zeros(WO); db2=zeros(C); dh1=malloc(B*H*4); dh1r=malloc(B*H*4)
dW1=zeros(WH); db1=zeros(H)
mW1=zeros(WH); vW1=zeros(WH); mW2=zeros(WO); vW2=zeros(WO)
mb1=zeros(H);  vb1=zeros(H);  mb2=zeros(C);  vb2=zeros(C)

xavier(W1,WH,IN,42); xavier(W2,WO,H,99)

# Class-band inputs
bw=IN//C
xvals=[0.1]*(B*IN)
for i in range(B):
    c=i%C
    for j in range(c*bw, min((c+1)*bw,IN)): xvals[i*IN+j]=0.9
wr(X,xvals)
wi(Y,[i%C for i in range(B)])

print(f"=== Full-scale test: B={B} IN={IN} H={H} C={C} ===")
print("Step | Mean Loss | Note")
print("-----|-----------|-----")

b1t,b2t=0.9,0.999
for step in range(25):
    # Forward: Y=X@W^T (W is [out x in]) -> gemm_nt
    fill(pre1,B*H,0.0)
    gemm_nt(X,W1,pre1,B,IN,H,1.0,0.0)
    bias(pre1,b1,B,H)
    relu_f(pre1,h1,B*H)
    fill(logits,B*C,0.0)
    gemm_nt(h1,W2,logits,B,H,C,1.0,0.0)
    bias(logits,b2,B,C)

    fill(loss_buf,B,0.0)
    ce_fwd(logits,Y,loss_buf,B,C)
    losses=rd(loss_buf,B)
    ml=sum(losses)/B
    note="" if step>0 else "(init, expect ~2.30)"
    if step>0:
        note="DOWN" if ml<prev_loss else "FLAT/UP"
    print(f"  {step:3d} | {ml:.6f}  | {note}")
    prev_loss=ml

    # Backward
    ce_bwd(logits,Y,dlogits,B,C)

    fill(dW2,WO,0.0); fill(db2,C,0.0)
    gemm_nt(dlogits,h1,dW2,C,B,H,1.0,0.0)
    reduce(dlogits,db2,B,C)
    fill(dh1,B*H,0.0)
    gemm(dlogits,W2,dh1,B,C,H,1.0,0.0)   # dX = dY @ W  (W is [out x in])
    relu_b(dh1,pre1,dh1r,B*H)
    fill(dW1,WH,0.0); fill(db1,H,0.0)
    gemm_nt(dh1r,X,dW1,H,B,IN,1.0,0.0)
    reduce(dh1r,db1,B,H)

    adam(W1,dW1,mW1,vW1,0.001,0.9,0.999,1e-7,b1t,b2t,WH)
    adam(b1,db1,mb1,vb1,0.001,0.9,0.999,1e-7,b1t,b2t,H)
    adam(W2,dW2,mW2,vW2,0.001,0.9,0.999,1e-7,b1t,b2t,WO)
    adam(b2,db2,mb2,vb2,0.001,0.9,0.999,1e-7,b1t,b2t,C)
    b1t*=0.9; b2t*=0.999

print()
print("SUCCESS if final loss << initial loss")
