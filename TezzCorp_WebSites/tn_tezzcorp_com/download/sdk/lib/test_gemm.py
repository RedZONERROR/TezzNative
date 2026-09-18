import ctypes
lib = ctypes.CDLL(r'C:\Acer\Code\TezzNative\TezzNative-language\run\tzcpu.dll')
i64=ctypes.c_int64; f32=ctypes.c_float

def fn(name,args,res=i64):
    ff=getattr(lib,name); ff.argtypes=args; ff.restype=res; return ff

malloc=fn('tz_cpu_malloc',[i64]); zeros=fn('tz_cpu_zeros',[i64])
fill=fn('tz_cpu_fill_f32',[i64,i64,f32])
xavier=fn('tz_cpu_xavier_init_f32',[i64,i64,i64,i64])
gemm=fn('tz_cpu_gemm_f32',[i64,i64,i64,i64,i64,i64,f32,f32])
gemm_nt=fn('tz_cpu_gemm_nt_f32',[i64,i64,i64,i64,i64,i64,f32,f32])

def rd(ptr,n): return list((ctypes.c_float*n).from_address(ptr))
def wr(ptr,vals):
    buf=(ctypes.c_float*len(vals)).from_address(ptr)
    for i,v in enumerate(vals): buf[i]=v

print("=== GEMM unit tests ===")

# Test 1: gemm_nt(A,B,C, M=2, K=3, N=2)  -> C = A[2x3] @ B[2x3]^T = [2x2]
# A = [[1,2,3],[4,5,6]], B = [[1,0,0],[0,1,0]]
# A@B^T = [[1*1+2*0+3*0, 1*0+2*1+3*0],[4*1+5*0+6*0, 4*0+5*1+6*0]]
#       = [[1, 2], [4, 5]]
M,K,N=2,3,2
A=malloc(M*K*4); B=malloc(N*K*4); C=zeros(M*N)
wr(A,[1.,2.,3.,4.,5.,6.])
wr(B,[1.,0.,0.,0.,1.,0.])
fill(C,M*N,0.)
gemm_nt(A,B,C,M,K,N,1.0,0.0)
print(f"gemm_nt [[1,2,3],[4,5,6]] @ [[1,0,0],[0,1,0]]^T")
print(f"  got:    {rd(C,M*N)}")
print(f"  expect: [1.0, 2.0, 4.0, 5.0]")
print()

# Test 2: regular gemm A[2x3] @ B[3x2]
C2=zeros(M*N); B2=malloc(K*N*4)
wr(B2,[1.,0.,0.,1.,0.,0.])  # [[1,0],[0,1],[0,0]]
fill(C2,M*N,0.)
gemm(A,B2,C2,M,K,N,1.0,0.0)
print(f"gemm [[1,2,3],[4,5,6]] @ [[1,0],[0,1],[0,0]]")
print(f"  got:    {rd(C2,M*N)}")
print(f"  expect: [1.0, 2.0, 4.0, 5.0]")
print()

# Test 3: Xavier init + gemm_nt at real scale
print("Xavier + gemm_nt (W[4x3], X[2x3] -> pre[2x4]):")
W=malloc(4*3*4); xavier(W,12,3,42)
wv=rd(W,12)
print(f"  W[0..3]: {[round(v,3) for v in wv[:4]]}")
X=malloc(2*3*4)
wr(X,[0.9,0.1,0.1, 0.1,0.9,0.1])
pre=zeros(2*4)
gemm_nt(X,W,pre,2,3,4,1.0,0.0)
pv=rd(pre,8)
print(f"  pre: {[round(v,4) for v in pv]}")
print(f"  (should be nonzero if W and X are nonzero)")
print()

# Manual verification of pre[0,0] = X[0,:] . W[0,:] (dot product)
xr=rd(X,6); wr_=rd(W,12)
manual_00 = sum(xr[j]*wr_[j] for j in range(3))
print(f"  Manual pre[0,0] = X[0].W[0] = {manual_00:.4f}")
print(f"  GEMM pre[0,0]   = {pv[0]:.4f}")
print(f"  Match: {abs(manual_00-pv[0])<1e-4}")
