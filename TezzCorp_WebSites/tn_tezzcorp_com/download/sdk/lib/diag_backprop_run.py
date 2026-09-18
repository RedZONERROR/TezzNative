"""
Backprop diagnostic using ctypes to call tzcpu.dll directly.
Tests a tiny B=4, IN=6, H=4, C=3 network through 25 gradient steps.
Prints: logits, softmax probs, losses, dlogits, dW2 at step 0
        and loss each epoch â€” if loss decreases, backprop is correct.
"""
import ctypes, os, struct, sys

# Load tzcpu.dll
dll_path = r"C:\Acer\Code\TezzNative\TezzNative-language\run\tzcpu.dll"
lib = ctypes.CDLL(dll_path)

# Define fn signatures (all take i64, return i64)
def fn(name, argtypes, restype=ctypes.c_int64):
    f = getattr(lib, name)
    f.argtypes = argtypes
    f.restype = restype
    return f

i64 = ctypes.c_int64
f32 = ctypes.c_float

malloc   = fn("tz_cpu_malloc",    [i64])
zeros    = fn("tz_cpu_zeros",     [i64])
fill     = fn("tz_cpu_fill_f32",  [i64, i64, f32])
xavier   = fn("tz_cpu_xavier_init_f32", [i64, i64, i64, i64])
gemm     = fn("tz_cpu_gemm_f32",  [i64]*6 + [f32, f32])
gemm_nt  = fn("tz_cpu_gemm_nt_f32", [i64]*6 + [f32, f32])
bias     = fn("tz_cpu_add_bias_f32", [i64, i64, i64, i64])
relu_f   = fn("tz_cpu_relu_f32",  [i64, i64, i64])
relu_bwd = fn("tz_cpu_relu_bwd_f32", [i64, i64, i64, i64])
ce_fwd   = fn("tz_cpu_cross_entropy_f32",     [i64]*5)
ce_bwd   = fn("tz_cpu_cross_entropy_bwd_f32", [i64]*5)
reduce   = fn("tz_cpu_reduce_sum_cols_f32",   [i64]*4)
adam     = fn("tz_cpu_adam_f32",  [i64]*4 + [f32]*6 + [i64])
report   = fn("tz_cpu_report_loss", [i64, i64, i64])

def read_floats(ptr, n):
    """Read n floats from raw pointer."""
    buf = (ctypes.c_float * n).from_address(ptr)
    return list(buf)

def write_floats(ptr, vals):
    buf = (ctypes.c_float * len(vals)).from_address(ptr)
    for i, v in enumerate(vals): buf[i] = v

def write_ints(ptr, vals):
    buf = (ctypes.c_int32 * len(vals)).from_address(ptr)
    for i, v in enumerate(vals): buf[i] = v

def fmt(vals, n=8):
    return " ".join(f"{v:.4f}" for v in vals[:n])

# â”€â”€â”€ Network dimensions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
B, IN, H, C = 4, 6, 4, 3
WH, WO = H * IN, C * H

print("=== TezzNative Backprop Diagnostic (Python/ctypes) ===")
print(f"Net: B={B} IN={IN} H={H} C={C} | WH={WH} WO={WO}")
print()

# Allocate all tensors
W1  = malloc(WH*4);  b1 = zeros(H)
W2  = malloc(WO*4);  b2 = zeros(C)
X   = malloc(B*IN*4); Y = malloc(B*4)
pre1= malloc(B*H*4); h1 = malloc(B*H*4)
logits=malloc(B*C*4); loss_buf=zeros(B)
dlogits=malloc(B*C*4)
dW2=zeros(WO); db2=zeros(C)
dh1=malloc(B*H*4); dh1r=malloc(B*H*4)
dW1=zeros(WH); db1=zeros(H)
mW1=zeros(WH); vW1=zeros(WH)
mW2=zeros(WO); vW2=zeros(WO)
mb1=zeros(H);  vb1=zeros(H)
mb2=zeros(C);  vb2=zeros(C)

# He init
xavier(W1, WH, IN, 42)
xavier(W2, WO, H,  99)
print(f"W1[0..{WH}]: {fmt(read_floats(W1, WH))}")
print(f"W2[0..{WO}]: {fmt(read_floats(W2, WO))}")
print(f"b1: {fmt(read_floats(b1, H))}")
print(f"b2: {fmt(read_floats(b2, C))}")
print()

# Diverse inputs: class k â†’ pixels[k*bw..(k+1)*bw]=0.9, rest=0.1
bw = IN // C
for i in range(B):
    row = [0.1] * IN
    cls = i % C
    for j in range(cls*bw, min((cls+1)*bw, IN)):
        row[j] = 0.9
    write_floats(X + i*IN*4, row)
# Cycling labels as int32
write_ints(Y, [i % C for i in range(B)])

print("Input X:")
for i in range(B):
    r = read_floats(X + i*IN*4, IN)
    print(f"  sample {i} (label {i%C}): {fmt(r)}")
print()

# â”€â”€â”€ Training loop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
print("Step | Mean Loss | (expect decrease)")
print("-----|-----------|------------------")

b1t, b2t = 0.9, 0.999
LR = 0.05  # high lr for quick convergence test

for step in range(30):
    # Forward
    fill(pre1, B*H, 0.0)
    gemm(X, W1, pre1, B, IN, H, 1.0, 0.0)
    bias(pre1, b1, B, H)
    relu_f(pre1, h1, B*H)
    fill(logits, B*C, 0.0)
    gemm(h1, W2, logits, B, H, C, 1.0, 0.0)
    bias(logits, b2, B, C)

    if step == 0:
        print(f"\n[Step 0 diagnostic]")
        print(f"  pre-softmax logits: {fmt(read_floats(logits, B*C))}")

    # Loss: cross_entropy_f32 converts logits->softmax in-place AND fills loss_buf
    fill(loss_buf, B, 0.0)
    ce_fwd(logits, Y, loss_buf, B, C)

    losses = read_floats(loss_buf, B)
    mean_loss = sum(losses) / B

    if step == 0:
        print(f"  softmax probs: {fmt(read_floats(logits, B*C))}")
        print(f"  per-sample loss: {fmt(losses, B)}")

    # Backward
    ce_bwd(logits, Y, dlogits, B, C)

    if step == 0:
        print(f"  dlogits: {fmt(read_floats(dlogits, B*C))}")

    fill(dW2, WO, 0.0); fill(db2, C, 0.0)
    gemm_nt(dlogits, h1, dW2, C, B, H, 1.0, 0.0)
    reduce(dlogits, db2, B, C)

    if step == 0:
        print(f"  dW2: {fmt(read_floats(dW2, WO))}")
        print(f"  db2: {fmt(read_floats(db2, C))}")
        dw2_norm = sum(v**2 for v in read_floats(dW2, WO))**0.5
        db2_vals = read_floats(db2, C)
        print(f"  |dW2|={dw2_norm:.4f}, db2={db2_vals}")

    fill(dh1, B*H, 0.0)
    gemm(dlogits, W2, dh1, B, C, H, 1.0, 0.0)
    relu_bwd(dh1, pre1, dh1r, B*H)

    fill(dW1, WH, 0.0); fill(db1, H, 0.0)
    gemm_nt(dh1r, X, dW1, H, B, IN, 1.0, 0.0)
    reduce(dh1r, db1, B, H)

    if step == 0:
        dw1_norm = sum(v**2 for v in read_floats(dW1, WH))**0.5
        print(f"  |dW1|={dw1_norm:.4f}")
        print()

    # Adam
    adam(W1, dW1, mW1, vW1, LR, 0.9, 0.999, 1e-8, b1t, b2t, WH)
    adam(b1, db1, mb1, vb1, LR, 0.9, 0.999, 1e-8, b1t, b2t, H)
    adam(W2, dW2, mW2, vW2, LR, 0.9, 0.999, 1e-8, b1t, b2t, WO)
    adam(b2, db2, mb2, vb2, LR, 0.9, 0.999, 1e-8, b1t, b2t, C)
    b1t *= 0.9; b2t *= 0.999

    status = "â¬‡ GOOD" if step > 0 else "init"
    print(f"  {step:3d}  | {mean_loss:.6f}  | {status}")

print()
print("=" * 50)
print("If loss went from ~1.1 down: backprop CORRECT")
print("If flat at 1.099: gradient is zero (bug)")
