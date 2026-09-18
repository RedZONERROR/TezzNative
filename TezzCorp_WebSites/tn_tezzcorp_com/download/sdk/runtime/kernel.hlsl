// TezzNative HLSL kernel template
// Compile with: dxc -T cs_6_6 -E main -Fo build\\kernel.dxil runtime\\kernel.hlsl
// Note: current HLSL backend supports int/char only (no double).

RWByteAddressBuffer g_mem : register(u0);

static uint64_t tn_u64(uint2 v){ return ((uint64_t)v.y << 32) | (uint64_t)v.x; }
static uint2 tn_u64_to_u2(uint64_t v){ return uint2((uint)(v & 0xffffffffu), (uint)(v >> 32)); }
static int64_t tn_load_i64(uint64_t addr){ uint2 v = g_mem.Load2((uint)addr); return (int64_t)tn_u64(v); }
static uint tn_load_u8(uint64_t addr){
  uint off = (uint)(addr & ~3ull);
  uint shift = (uint)(addr & 3ull) * 8u;
  uint v = g_mem.Load(off);
  return (v >> shift) & 0xFFu;
}
static void tn_store_i64(uint64_t addr, int64_t v){ g_mem.Store2((uint)addr, tn_u64_to_u2((uint64_t)v)); }
static void tn_store_u8(uint64_t addr, uint v){
  uint off = (uint)(addr & ~3ull);
  uint shift = (uint)(addr & 3ull) * 8u;
  uint cur = g_mem.Load(off);
  cur = (cur & ~(0xFFu << shift)) | ((v & 0xFFu) << shift);
  g_mem.Store(off, cur);
}

// Example kernel (replace as needed)
[numthreads(1,1,1)]
void main(uint3 DTid : SV_DispatchThreadID){
  // use DTid.x for 1D thread index
  // read/write g_mem using the helpers above
}
