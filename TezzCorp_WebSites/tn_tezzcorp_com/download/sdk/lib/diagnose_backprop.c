// lib/diagnose_backprop.c
// Standalone C diagnostic: tests each kernel in the gradient chain individually
// using only tzcpu.dll functions. Prints values at each step to locate the bug.
//
// Build:  cl /O0 /nologo diagnose_backprop.c /I. /link run\tzcpu.lib
// Run:    diagnose_backprop.exe

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE LibHandle;
#define LOAD_LIB(p)   LoadLibraryA(p)
#define GET_SYM(h,n)  GetProcAddress(h,n)
#else
#include <dlfcn.h>
typedef void* LibHandle;
#define LOAD_LIB(p)   dlopen(p,RTLD_NOW)
#define GET_SYM(h,n)  dlsym(h,n)
#endif

typedef int64_t(*Fn0)(void);
typedef int64_t(*Fn1)(int64_t);
typedef int64_t(*Fn2)(int64_t,int64_t);
typedef int64_t(*Fn3)(int64_t,int64_t,int64_t);
typedef int64_t(*FnFill)(int64_t,int64_t,double);
typedef int64_t(*FnGemm)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,double,double);
typedef int64_t(*FnBias)(int64_t,int64_t,int64_t,int64_t);
typedef int64_t(*FnCE)(int64_t,int64_t,int64_t,int64_t,int64_t);
typedef int64_t(*FnReport)(int64_t,int64_t,int64_t);
typedef int64_t(*FnXavier)(int64_t,int64_t,int64_t,int64_t);
typedef int64_t(*FnAdam)(int64_t,int64_t,int64_t,int64_t,double,double,double,double,double,double,int64_t);
typedef int64_t(*FnReduce)(int64_t,int64_t,int64_t,int64_t);

static LibHandle lib;
#define SYM(T,name) ((T)GET_SYM(lib,name))
#define fp(p)  ((float*)(intptr_t)(p))

void print_buf(const char* tag, int64_t ptr, int n){
    float *p=fp(ptr);
    printf("  %-20s [%d..%d]: ", tag, 0, n-1<7?n-1:7);
    int show=n<8?n:8;
    for(int i=0;i<show;i++) printf("%.4f ", p[i]);
    if(n>8) printf("...");
    printf("\n");
}

int main(){
    printf("=== TezzNative Backprop Diagnostic ===\n\n");

    // Load tzcpu.dll
#ifdef _WIN32
    lib=LOAD_LIB("run\\tzcpu.dll");
#else
    lib=LOAD_LIB("run/libtzcpu.so");
#endif
    if(!lib){ printf("FATAL: cannot load tzcpu.dll\n"); return 1; }

    // Tiny dimensions for easy inspection
    int B=4, IN=8, H=4, C=3;  // batch=4, in=8, hidden=4, classes=3
    int WH=H*IN, WO=C*H;

    // Alloc everything
    int64_t W1 = SYM(Fn1,"tz_cpu_malloc")((int64_t)(WH*4));
    int64_t b1 = SYM(Fn1,"tz_cpu_zeros")((int64_t)H);
    int64_t W2 = SYM(Fn1,"tz_cpu_malloc")((int64_t)(WO*4));
    int64_t b2 = SYM(Fn1,"tz_cpu_zeros")((int64_t)C);
    int64_t X  = SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*IN*4));
    int64_t Y  = SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*4));    // labels i32
    int64_t pre1=SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*H*4));
    int64_t h1  =SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*H*4));
    int64_t logits=SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*C*4));
    int64_t loss_buf=SYM(Fn1,"tz_cpu_zeros")((int64_t)B);
    int64_t dlogits=SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*C*4));
    int64_t dW2=SYM(Fn1,"tz_cpu_zeros")((int64_t)WO);
    int64_t db2=SYM(Fn1,"tz_cpu_zeros")((int64_t)C);
    int64_t dh1=SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*H*4));
    int64_t dh1r=SYM(Fn1,"tz_cpu_malloc")((int64_t)(B*H*4));
    int64_t dW1=SYM(Fn1,"tz_cpu_zeros")((int64_t)WH);
    int64_t db1=SYM(Fn1,"tz_cpu_zeros")((int64_t)H);

    printf("[STEP 1] Xavier init W1[%d], W2[%d]\n", WH, WO);
    SYM(FnXavier,"tz_cpu_xavier_init_f32")(W1,(int64_t)WH,(int64_t)IN,42);
    SYM(FnXavier,"tz_cpu_xavier_init_f32")(W2,(int64_t)WO,(int64_t)H,99);
    print_buf("W1", W1, WH);
    print_buf("W2", W2, WO);
    print_buf("b1", b1, H);
    print_buf("b2", b2, C);

    printf("\n[STEP 2] Fill inputs by class pattern (B=%d, IN=%d, C=%d)\n",B,IN,C);
    // Manual fill: class 0 → pixels[0..2]=0.9, class 1 → pixels[3..5]=0.9, etc.
    {
        float *xp=fp(X);
        int bw=IN/C;
        for(int i=0;i<B;i++){
            int cls=i%C;
            for(int j=0;j<IN;j++) xp[i*IN+j]=0.1f;
            for(int j=cls*bw;j<(cls+1)*bw&&j<IN;j++) xp[i*IN+j]=0.9f;
        }
        // Cycling labels
        int *yp=(int*)(intptr_t)Y;
        for(int i=0;i<B;i++) yp[i]=i%C;
    }
    print_buf("X[0]", X, IN);
    printf("  labels: ");
    int *yp=(int*)(intptr_t)Y;
    for(int i=0;i<B;i++) printf("%d ", yp[i]);
    printf("\n");

    // Run 20 gradient steps and print loss each time
    printf("\n[STEP 3] Training 20 steps (expect loss to decrease from ~1.1)\n");
    float b1t=0.9f, b2t=0.999f;
    int64_t mW1=SYM(Fn1,"tz_cpu_zeros")((int64_t)WH);
    int64_t vW1=SYM(Fn1,"tz_cpu_zeros")((int64_t)WH);
    int64_t mW2=SYM(Fn1,"tz_cpu_zeros")((int64_t)WO);
    int64_t vW2=SYM(Fn1,"tz_cpu_zeros")((int64_t)WO);
    int64_t mb1=SYM(Fn1,"tz_cpu_zeros")((int64_t)H);
    int64_t vb1=SYM(Fn1,"tz_cpu_zeros")((int64_t)H);
    int64_t mb2=SYM(Fn1,"tz_cpu_zeros")((int64_t)C);
    int64_t vb2=SYM(Fn1,"tz_cpu_zeros")((int64_t)C);

    for(int step=0;step<20;step++){
        // Forward
        SYM(FnFill,"tz_cpu_fill_f32")(pre1,(int64_t)(B*H),0.0);
        SYM(FnGemm,"tz_cpu_gemm_f32")(X,W1,pre1,(int64_t)B,(int64_t)IN,(int64_t)H,1.0,0.0);
        SYM(FnBias,"tz_cpu_add_bias_f32")(pre1,b1,(int64_t)B,(int64_t)H);
        SYM(Fn3,"tz_cpu_relu_f32")(pre1,h1,(int64_t)(B*H));
        SYM(FnFill,"tz_cpu_fill_f32")(logits,(int64_t)(B*C),0.0);
        SYM(FnGemm,"tz_cpu_gemm_f32")(h1,W2,logits,(int64_t)B,(int64_t)H,(int64_t)C,1.0,0.0);
        SYM(FnBias,"tz_cpu_add_bias_f32")(logits,b2,(int64_t)B,(int64_t)C);

        // Loss (modifies logits → softmax probs in-place)
        SYM(FnFill,"tz_cpu_fill_f32")(loss_buf,(int64_t)B,0.0);
        SYM(FnCE,"tz_cpu_cross_entropy_f32")(logits,Y,loss_buf,(int64_t)B,(int64_t)C);
        int64_t loss_x10k = SYM(FnReport,"tz_cpu_report_loss")(loss_buf,(int64_t)B,(int64_t)step);

        // Check if dlogits look right before backward
        if(step==0){
            SYM(FnCE,"tz_cpu_cross_entropy_bwd_f32")(logits,Y,dlogits,(int64_t)B,(int64_t)C);
            printf("  [step 0 diagnostic]\n");
            print_buf("  logits(softmax)", logits, B*C);
            print_buf("  dlogits", dlogits, B*C);
        }

        // Backward
        SYM(FnCE,"tz_cpu_cross_entropy_bwd_f32")(logits,Y,dlogits,(int64_t)B,(int64_t)C);

        SYM(FnFill,"tz_cpu_fill_f32")(dW2,(int64_t)WO,0.0);
        SYM(FnFill,"tz_cpu_fill_f32")(db2,(int64_t)C,0.0);
        SYM(FnGemm,"tz_cpu_gemm_nt_f32")(dlogits,h1,dW2,(int64_t)C,(int64_t)B,(int64_t)H,1.0,0.0);
        SYM(FnReduce,"tz_cpu_reduce_sum_cols_f32")(dlogits,db2,(int64_t)B,(int64_t)C);
        SYM(FnFill,"tz_cpu_fill_f32")(dh1,(int64_t)(B*H),0.0);
        SYM(FnGemm,"tz_cpu_gemm_f32")(dlogits,W2,dh1,(int64_t)B,(int64_t)C,(int64_t)H,1.0,0.0);
        SYM(Fn3,"tz_cpu_relu_bwd_f32")(dh1,pre1,dh1r,(int64_t)(B*H));

        SYM(FnFill,"tz_cpu_fill_f32")(dW1,(int64_t)WH,0.0);
        SYM(FnFill,"tz_cpu_fill_f32")(db1,(int64_t)H,0.0);
        SYM(FnGemm,"tz_cpu_gemm_nt_f32")(dh1r,X,dW1,(int64_t)H,(int64_t)B,(int64_t)IN,1.0,0.0);
        SYM(FnReduce,"tz_cpu_reduce_sum_cols_f32")(dh1r,db1,(int64_t)B,(int64_t)H);

        if(step==0){
            print_buf("  dW2", dW2, WO);
            print_buf("  db2", db2, C);
            print_buf("  dW1", dW1, WH);
            print_buf("  db1", db1, H);
        }

        // Adam
        SYM(FnAdam,"tz_cpu_adam_f32")(W1,dW1,mW1,vW1,0.01,0.9,0.999,1e-8,b1t,b2t,(int64_t)WH);
        SYM(FnAdam,"tz_cpu_adam_f32")(b1,db1,mb1,vb1,0.01,0.9,0.999,1e-8,b1t,b2t,(int64_t)H);
        SYM(FnAdam,"tz_cpu_adam_f32")(W2,dW2,mW2,vW2,0.01,0.9,0.999,1e-8,b1t,b2t,(int64_t)WO);
        SYM(FnAdam,"tz_cpu_adam_f32")(b2,db2,mb2,vb2,0.01,0.9,0.999,1e-8,b1t,b2t,(int64_t)C);

        b1t*=0.9f; b2t*=0.999f;
    }

    printf("\n=== If loss went DOWN: backprop is correct! ===\n");
    printf("=== If loss is flat: there is a kernel bug ===\n");
    return 0;
}
