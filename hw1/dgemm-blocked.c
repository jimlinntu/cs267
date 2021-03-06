#include <immintrin.h>
#include <assert.h>

#define CACHELINE 8
#define BLOCK_SIZE 168

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define EXPERIMENT 16
#define min(a, b) (((a) < (b)) ? (a) : (b))

#if EXPERIMENT != 9
const char* dgemm_desc = "Blocking experiment: " STRINGIFY(EXPERIMENT) ", block_size: " STRINGIFY(BLOCK_SIZE);
#endif

inline void cpy(int N_pad, int N, double *from, double *to){
    for(int j = 0; j < N; ++j){
        for(int i = 0; i < N; ++i){
            to[i + j * N_pad] = from[i + j * N];
        }
        for(int i = N; i < N_pad; ++i){
            to[i + j * N_pad] = 0;
        }
    }
    for(int j = N; j < N_pad; j++){
        for(int i = 0; i < N_pad; i++){
            to[i + j * N_pad] = 0;
        }
    }
}
inline void cpy_avx(int N_pad, int N, double *from, double *to){
    for(int j = 0; j < N; ++j){
        int remainder = N % CACHELINE;
        for(int i = 0; i < N - remainder; i += CACHELINE){
            __m512d l = _mm512_loadu_pd(from + i + j * N);
            _mm512_store_pd(to + i + j * N_pad, l);
        }
        for(int i = N - remainder; i < N; ++i){
            to[i + j * N_pad] = from[i + j * N];
        }
        for(int i = N; i < N_pad; ++i){
            to[i + j * N_pad] = 0;
        }
    }
    for(int j = N; j < N_pad; j++){
        for(int i = 0; i < N_pad; i++){
            to[i + j * N_pad] = 0;
        }
    }
}

inline void transpose_cpy(int N_pad, int N, double *from, double *to){
    for(int j = 0; j < N; ++j){
        for(int i = 0; i < N; ++i){
            to[j + i * N_pad] = from[i + j * N];
        }
        for(int i = N; i < N_pad; ++i){
            to[j + i * N_pad] = 0;
        }
    }
    for(int i = 0; i < N_pad; i++){
        for(int j = N; j < N_pad; j++){
            to[j + i * N_pad] = 0;
        }
    }
}

void square_dgemm_starter_code_modified(int lda, double* A, double* B, double* C){
    for(int i = 0; i < lda; i += BLOCK_SIZE){
        for(int j = 0; j < lda; j += BLOCK_SIZE){
            for(int k = 0; k < lda; k += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                int M = min(BLOCK_SIZE, lda - i);
                int N = min(BLOCK_SIZE, lda - j);
                int K = min(BLOCK_SIZE, lda - k);

                double *A_block = A + i + k * lda;
                double *B_block = B + k + j * lda;
                double *C_block = C + i + j * lda;

				for(int ii = 0; ii < M; ++ii){
                    double *A_row = A_block + ii;
                    double *C_col = C_block + ii;

					for(int jj = 0; jj < N; ++jj){
                        double *B_col = B_block + jj * lda;
                        double *C_element = C_col + jj * lda;
                        double c = 0;
						for(int kk = 0; kk < K; ++kk){
                            c += A_row[kk * lda] * B_col[kk];
                        }
                        *C_element += c;
                    }
                }
            }
        }
    }
}

void square_dgemm_block_jki(int N, double* A, double* B, double* C) {

    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int i = 0; i < N_pad; i += BLOCK_SIZE){
        for(int j = 0; j < N_pad; j += BLOCK_SIZE){
            for(int k = 0; k < N_pad; k += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

void square_dgemm_block_kji(int N, double* A, double* B, double* C) {

    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int i = 0; i < N_pad; i += BLOCK_SIZE){
        for(int j = 0; j < N_pad; j += BLOCK_SIZE){
            for(int k = 0; k < N_pad; k += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                    double *A_col = A_block + kk * N_pad;
                    double *B_row = B_block + kk;

                    for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                        double *B_element = B_row + jj * N_pad;
                        double *C_col = C_block + jj * N_pad;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}
#define H 24
#define W 8
// # of registers used
// A: (H / 8) registers
// B: 2 registers (because only two vector lanes)
// C: (H / 8) * 8
// When H = 24 -> 3 + 2 + 3 * 8 = 29 registers

void square_dgemm_microkernel(int N, double* A, double* B, double* C){
    int N_pad = (N + H - 1) / H * H;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int i = 0; i < N_pad; i += BLOCK_SIZE){
        for(int j = 0; j < N_pad; j += BLOCK_SIZE){
            for(int k = 0; k < N_pad; k += BLOCK_SIZE){
                const int M = min(N_pad - i, BLOCK_SIZE);
                const int N = min(N_pad - j, BLOCK_SIZE);
                const int K = min(N_pad - k, BLOCK_SIZE);

                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;

                for(int ii = 0; ii < M; ii += H){
                    double *A_panel = A_block + ii;
                    for(int jj = 0; jj < N; jj += W){
                        double *B_panel = B_block + jj * N_pad;
                        double *C_block_small = C_block + ii + jj * N_pad;
/*[[[cog
import cog

H = 24
W = 8
NUM_B_REGISTERS = 2
CACHELINE = 8

# Declare registers for A
for i in range(H // CACHELINE):
    cog.out(
    """
    __m512d a{i};
    """.format(i=i)
    )
# Declare registers for B
for j in range(NUM_B_REGISTERS):
    cog.out(
    """
    __m512d b{j};
    """.format(j=j))
# Declare registers for C
for i in range(H // CACHELINE):
    for j in range(W):
        cog.out(
        """
        __m512d c{i}{j};
        c{i}{j} = _mm512_load_pd(C_block_small + {i} * {CACHELINE} + {j} * N_pad);
        """.format(i=i, j=j, CACHELINE=CACHELINE)
        )

]]]*/

__m512d a0;

__m512d a1;

__m512d a2;

__m512d b0;

__m512d b1;

    __m512d c00;
    c00 = _mm512_load_pd(C_block_small + 0 * 8 + 0 * N_pad);
    
    __m512d c01;
    c01 = _mm512_load_pd(C_block_small + 0 * 8 + 1 * N_pad);
    
    __m512d c02;
    c02 = _mm512_load_pd(C_block_small + 0 * 8 + 2 * N_pad);
    
    __m512d c03;
    c03 = _mm512_load_pd(C_block_small + 0 * 8 + 3 * N_pad);
    
    __m512d c04;
    c04 = _mm512_load_pd(C_block_small + 0 * 8 + 4 * N_pad);
    
    __m512d c05;
    c05 = _mm512_load_pd(C_block_small + 0 * 8 + 5 * N_pad);
    
    __m512d c06;
    c06 = _mm512_load_pd(C_block_small + 0 * 8 + 6 * N_pad);
    
    __m512d c07;
    c07 = _mm512_load_pd(C_block_small + 0 * 8 + 7 * N_pad);
    
    __m512d c10;
    c10 = _mm512_load_pd(C_block_small + 1 * 8 + 0 * N_pad);
    
    __m512d c11;
    c11 = _mm512_load_pd(C_block_small + 1 * 8 + 1 * N_pad);
    
    __m512d c12;
    c12 = _mm512_load_pd(C_block_small + 1 * 8 + 2 * N_pad);
    
    __m512d c13;
    c13 = _mm512_load_pd(C_block_small + 1 * 8 + 3 * N_pad);
    
    __m512d c14;
    c14 = _mm512_load_pd(C_block_small + 1 * 8 + 4 * N_pad);
    
    __m512d c15;
    c15 = _mm512_load_pd(C_block_small + 1 * 8 + 5 * N_pad);
    
    __m512d c16;
    c16 = _mm512_load_pd(C_block_small + 1 * 8 + 6 * N_pad);
    
    __m512d c17;
    c17 = _mm512_load_pd(C_block_small + 1 * 8 + 7 * N_pad);
    
    __m512d c20;
    c20 = _mm512_load_pd(C_block_small + 2 * 8 + 0 * N_pad);
    
    __m512d c21;
    c21 = _mm512_load_pd(C_block_small + 2 * 8 + 1 * N_pad);
    
    __m512d c22;
    c22 = _mm512_load_pd(C_block_small + 2 * 8 + 2 * N_pad);
    
    __m512d c23;
    c23 = _mm512_load_pd(C_block_small + 2 * 8 + 3 * N_pad);
    
    __m512d c24;
    c24 = _mm512_load_pd(C_block_small + 2 * 8 + 4 * N_pad);
    
    __m512d c25;
    c25 = _mm512_load_pd(C_block_small + 2 * 8 + 5 * N_pad);
    
    __m512d c26;
    c26 = _mm512_load_pd(C_block_small + 2 * 8 + 6 * N_pad);
    
    __m512d c27;
    c27 = _mm512_load_pd(C_block_small + 2 * 8 + 7 * N_pad);
    
//[[[end]]]
                        for(int kk = 0; kk < K; ++kk){
                            double *A_col = A_panel + kk * N_pad;
                            double *B_row = B_panel + kk;
/*[[[cog
import cog
H = 24
W = 8
NUM_B_REGISTERS = 2
CACHELINE = 8

# Load A
for i in range(H // CACHELINE):
    cog.out(
    """
        a{i} = _mm512_load_pd(A_col + {i} * {CACHELINE});
    """.format(i=i, CACHELINE=CACHELINE)
    )

for i in range(H // CACHELINE):
    for j in range(W // NUM_B_REGISTERS):
        # Load B
        for k in range(NUM_B_REGISTERS):
            cog.out(
            """
            b{k} = _mm512_set1_pd(B_row[{j} * N_pad]);
            """.format(k=k, j=j*NUM_B_REGISTERS + k)
            )
        # Compute C and store C
        for k in range(NUM_B_REGISTERS):
            cog.out(
            """
            c{i}{j} = _mm512_fmadd_pd(a{i}, b{k}, c{i}{j});
            """.format(i=i, j=NUM_B_REGISTERS * j + k, k=k, CACHELINE=CACHELINE)
            )



]]]*/

a0 = _mm512_load_pd(A_col + 0 * 8);
    
a1 = _mm512_load_pd(A_col + 1 * 8);
    
a2 = _mm512_load_pd(A_col + 2 * 8);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c00 = _mm512_fmadd_pd(a0, b0, c00);
    
    c01 = _mm512_fmadd_pd(a0, b1, c01);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c02 = _mm512_fmadd_pd(a0, b0, c02);
    
    c03 = _mm512_fmadd_pd(a0, b1, c03);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c04 = _mm512_fmadd_pd(a0, b0, c04);
    
    c05 = _mm512_fmadd_pd(a0, b1, c05);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c06 = _mm512_fmadd_pd(a0, b0, c06);
    
    c07 = _mm512_fmadd_pd(a0, b1, c07);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c10 = _mm512_fmadd_pd(a1, b0, c10);
    
    c11 = _mm512_fmadd_pd(a1, b1, c11);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c12 = _mm512_fmadd_pd(a1, b0, c12);
    
    c13 = _mm512_fmadd_pd(a1, b1, c13);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c14 = _mm512_fmadd_pd(a1, b0, c14);
    
    c15 = _mm512_fmadd_pd(a1, b1, c15);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c16 = _mm512_fmadd_pd(a1, b0, c16);
    
    c17 = _mm512_fmadd_pd(a1, b1, c17);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c20 = _mm512_fmadd_pd(a2, b0, c20);
    
    c21 = _mm512_fmadd_pd(a2, b1, c21);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c22 = _mm512_fmadd_pd(a2, b0, c22);
    
    c23 = _mm512_fmadd_pd(a2, b1, c23);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c24 = _mm512_fmadd_pd(a2, b0, c24);
    
    c25 = _mm512_fmadd_pd(a2, b1, c25);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c26 = _mm512_fmadd_pd(a2, b0, c26);
    
    c27 = _mm512_fmadd_pd(a2, b1, c27);
    
//[[[end]]]
                        }
                        
/*[[[cog
import cog
H = 24
W = 8
CACHELINE = 8
# Store the C register back
for i in range(H // CACHELINE):
    for j in range(W):
        cog.out(
        """
        _mm512_store_pd(C_block_small + {i} * {CACHELINE} + {j} * N_pad, c{i}{j});
        """.format(i=i,j=j, CACHELINE=CACHELINE)
        )
]]]*/

_mm512_store_pd(C_block_small + 0 * 8 + 0 * N_pad, c00);

_mm512_store_pd(C_block_small + 0 * 8 + 1 * N_pad, c01);

_mm512_store_pd(C_block_small + 0 * 8 + 2 * N_pad, c02);

_mm512_store_pd(C_block_small + 0 * 8 + 3 * N_pad, c03);

_mm512_store_pd(C_block_small + 0 * 8 + 4 * N_pad, c04);

_mm512_store_pd(C_block_small + 0 * 8 + 5 * N_pad, c05);

_mm512_store_pd(C_block_small + 0 * 8 + 6 * N_pad, c06);

_mm512_store_pd(C_block_small + 0 * 8 + 7 * N_pad, c07);

_mm512_store_pd(C_block_small + 1 * 8 + 0 * N_pad, c10);

_mm512_store_pd(C_block_small + 1 * 8 + 1 * N_pad, c11);

_mm512_store_pd(C_block_small + 1 * 8 + 2 * N_pad, c12);

_mm512_store_pd(C_block_small + 1 * 8 + 3 * N_pad, c13);

_mm512_store_pd(C_block_small + 1 * 8 + 4 * N_pad, c14);

_mm512_store_pd(C_block_small + 1 * 8 + 5 * N_pad, c15);

_mm512_store_pd(C_block_small + 1 * 8 + 6 * N_pad, c16);

_mm512_store_pd(C_block_small + 1 * 8 + 7 * N_pad, c17);

_mm512_store_pd(C_block_small + 2 * 8 + 0 * N_pad, c20);

_mm512_store_pd(C_block_small + 2 * 8 + 1 * N_pad, c21);

_mm512_store_pd(C_block_small + 2 * 8 + 2 * N_pad, c22);

_mm512_store_pd(C_block_small + 2 * 8 + 3 * N_pad, c23);

_mm512_store_pd(C_block_small + 2 * 8 + 4 * N_pad, c24);

_mm512_store_pd(C_block_small + 2 * 8 + 5 * N_pad, c25);

_mm512_store_pd(C_block_small + 2 * 8 + 6 * N_pad, c26);

_mm512_store_pd(C_block_small + 2 * 8 + 7 * N_pad, c27);

//[[[end]]]
                    }
                }
            }
        }
    }

    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

void square_dgemm_jki_microkernel(int N, double* A, double* B, double* C){
    int N_pad = (N + H - 1) / H * H;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                const int M = min(N_pad - i, BLOCK_SIZE);
                const int N = min(N_pad - j, BLOCK_SIZE);
                const int K = min(N_pad - k, BLOCK_SIZE);

                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;

                for(int ii = 0; ii < M; ii += H){
                    for(int jj = 0; jj < N; jj += W){
                        double *A_panel = A_block + ii;
                        double *B_panel = B_block + jj * N_pad;
                        double *C_block_small = C_block + ii + jj * N_pad;
/*[[[cog
import cog

H = 24
W = 8
NUM_B_REGISTERS = 2
CACHELINE = 8

# Declare registers for A
for i in range(H // CACHELINE):
    cog.out(
    """
    __m512d a{i};
    """.format(i=i)
    )
# Declare registers for B
for j in range(NUM_B_REGISTERS):
    cog.out(
    """
    __m512d b{j};
    """.format(j=j))
# Declare registers for C
for i in range(H // CACHELINE):
    for j in range(W):
        cog.out(
        """
        __m512d c{i}{j};
        c{i}{j} = _mm512_load_pd(C_block_small + {i} * {CACHELINE} + {j} * N_pad);
        """.format(i=i, j=j, CACHELINE=CACHELINE)
        )

]]]*/

__m512d a0;

__m512d a1;

__m512d a2;

__m512d b0;

__m512d b1;

    __m512d c00;
    c00 = _mm512_load_pd(C_block_small + 0 * 8 + 0 * N_pad);
    
    __m512d c01;
    c01 = _mm512_load_pd(C_block_small + 0 * 8 + 1 * N_pad);
    
    __m512d c02;
    c02 = _mm512_load_pd(C_block_small + 0 * 8 + 2 * N_pad);
    
    __m512d c03;
    c03 = _mm512_load_pd(C_block_small + 0 * 8 + 3 * N_pad);
    
    __m512d c04;
    c04 = _mm512_load_pd(C_block_small + 0 * 8 + 4 * N_pad);
    
    __m512d c05;
    c05 = _mm512_load_pd(C_block_small + 0 * 8 + 5 * N_pad);
    
    __m512d c06;
    c06 = _mm512_load_pd(C_block_small + 0 * 8 + 6 * N_pad);
    
    __m512d c07;
    c07 = _mm512_load_pd(C_block_small + 0 * 8 + 7 * N_pad);
    
    __m512d c10;
    c10 = _mm512_load_pd(C_block_small + 1 * 8 + 0 * N_pad);
    
    __m512d c11;
    c11 = _mm512_load_pd(C_block_small + 1 * 8 + 1 * N_pad);
    
    __m512d c12;
    c12 = _mm512_load_pd(C_block_small + 1 * 8 + 2 * N_pad);
    
    __m512d c13;
    c13 = _mm512_load_pd(C_block_small + 1 * 8 + 3 * N_pad);
    
    __m512d c14;
    c14 = _mm512_load_pd(C_block_small + 1 * 8 + 4 * N_pad);
    
    __m512d c15;
    c15 = _mm512_load_pd(C_block_small + 1 * 8 + 5 * N_pad);
    
    __m512d c16;
    c16 = _mm512_load_pd(C_block_small + 1 * 8 + 6 * N_pad);
    
    __m512d c17;
    c17 = _mm512_load_pd(C_block_small + 1 * 8 + 7 * N_pad);
    
    __m512d c20;
    c20 = _mm512_load_pd(C_block_small + 2 * 8 + 0 * N_pad);
    
    __m512d c21;
    c21 = _mm512_load_pd(C_block_small + 2 * 8 + 1 * N_pad);
    
    __m512d c22;
    c22 = _mm512_load_pd(C_block_small + 2 * 8 + 2 * N_pad);
    
    __m512d c23;
    c23 = _mm512_load_pd(C_block_small + 2 * 8 + 3 * N_pad);
    
    __m512d c24;
    c24 = _mm512_load_pd(C_block_small + 2 * 8 + 4 * N_pad);
    
    __m512d c25;
    c25 = _mm512_load_pd(C_block_small + 2 * 8 + 5 * N_pad);
    
    __m512d c26;
    c26 = _mm512_load_pd(C_block_small + 2 * 8 + 6 * N_pad);
    
    __m512d c27;
    c27 = _mm512_load_pd(C_block_small + 2 * 8 + 7 * N_pad);
    
//[[[end]]]
                        for(int kk = 0; kk < K; ++kk){
                            double *A_col = A_panel + kk * N_pad;
                            double *B_row = B_panel + kk;
/*[[[cog
import cog
H = 24
W = 8
NUM_B_REGISTERS = 2
CACHELINE = 8

# Load A
for i in range(H // CACHELINE):
    cog.out(
    """
        a{i} = _mm512_load_pd(A_col + {i} * {CACHELINE});
    """.format(i=i, CACHELINE=CACHELINE)
    )

for i in range(H // CACHELINE):
    for j in range(W // NUM_B_REGISTERS):
        # Load B
        for k in range(NUM_B_REGISTERS):
            cog.out(
            """
            b{k} = _mm512_set1_pd(B_row[{j} * N_pad]);
            """.format(k=k, j=j*NUM_B_REGISTERS + k)
            )
        # Compute C and store C
        for k in range(NUM_B_REGISTERS):
            cog.out(
            """
            c{i}{j} = _mm512_fmadd_pd(a{i}, b{k}, c{i}{j});
            """.format(i=i, j=NUM_B_REGISTERS * j + k, k=k, CACHELINE=CACHELINE)
            )



]]]*/

a0 = _mm512_load_pd(A_col + 0 * 8);
    
a1 = _mm512_load_pd(A_col + 1 * 8);
    
a2 = _mm512_load_pd(A_col + 2 * 8);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c00 = _mm512_fmadd_pd(a0, b0, c00);
    
    c01 = _mm512_fmadd_pd(a0, b1, c01);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c02 = _mm512_fmadd_pd(a0, b0, c02);
    
    c03 = _mm512_fmadd_pd(a0, b1, c03);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c04 = _mm512_fmadd_pd(a0, b0, c04);
    
    c05 = _mm512_fmadd_pd(a0, b1, c05);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c06 = _mm512_fmadd_pd(a0, b0, c06);
    
    c07 = _mm512_fmadd_pd(a0, b1, c07);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c10 = _mm512_fmadd_pd(a1, b0, c10);
    
    c11 = _mm512_fmadd_pd(a1, b1, c11);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c12 = _mm512_fmadd_pd(a1, b0, c12);
    
    c13 = _mm512_fmadd_pd(a1, b1, c13);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c14 = _mm512_fmadd_pd(a1, b0, c14);
    
    c15 = _mm512_fmadd_pd(a1, b1, c15);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c16 = _mm512_fmadd_pd(a1, b0, c16);
    
    c17 = _mm512_fmadd_pd(a1, b1, c17);
    
    b0 = _mm512_set1_pd(B_row[0 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[1 * N_pad]);
    
    c20 = _mm512_fmadd_pd(a2, b0, c20);
    
    c21 = _mm512_fmadd_pd(a2, b1, c21);
    
    b0 = _mm512_set1_pd(B_row[2 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[3 * N_pad]);
    
    c22 = _mm512_fmadd_pd(a2, b0, c22);
    
    c23 = _mm512_fmadd_pd(a2, b1, c23);
    
    b0 = _mm512_set1_pd(B_row[4 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[5 * N_pad]);
    
    c24 = _mm512_fmadd_pd(a2, b0, c24);
    
    c25 = _mm512_fmadd_pd(a2, b1, c25);
    
    b0 = _mm512_set1_pd(B_row[6 * N_pad]);
    
    b1 = _mm512_set1_pd(B_row[7 * N_pad]);
    
    c26 = _mm512_fmadd_pd(a2, b0, c26);
    
    c27 = _mm512_fmadd_pd(a2, b1, c27);
    
//[[[end]]]
                        }
                        
/*[[[cog
import cog
H = 24
W = 8
CACHELINE = 8
# Store the C register back
for i in range(H // CACHELINE):
    for j in range(W):
        cog.out(
        """
        _mm512_store_pd(C_block_small + {i} * {CACHELINE} + {j} * N_pad, c{i}{j});
        """.format(i=i,j=j, CACHELINE=CACHELINE)
        )
]]]*/

_mm512_store_pd(C_block_small + 0 * 8 + 0 * N_pad, c00);

_mm512_store_pd(C_block_small + 0 * 8 + 1 * N_pad, c01);

_mm512_store_pd(C_block_small + 0 * 8 + 2 * N_pad, c02);

_mm512_store_pd(C_block_small + 0 * 8 + 3 * N_pad, c03);

_mm512_store_pd(C_block_small + 0 * 8 + 4 * N_pad, c04);

_mm512_store_pd(C_block_small + 0 * 8 + 5 * N_pad, c05);

_mm512_store_pd(C_block_small + 0 * 8 + 6 * N_pad, c06);

_mm512_store_pd(C_block_small + 0 * 8 + 7 * N_pad, c07);

_mm512_store_pd(C_block_small + 1 * 8 + 0 * N_pad, c10);

_mm512_store_pd(C_block_small + 1 * 8 + 1 * N_pad, c11);

_mm512_store_pd(C_block_small + 1 * 8 + 2 * N_pad, c12);

_mm512_store_pd(C_block_small + 1 * 8 + 3 * N_pad, c13);

_mm512_store_pd(C_block_small + 1 * 8 + 4 * N_pad, c14);

_mm512_store_pd(C_block_small + 1 * 8 + 5 * N_pad, c15);

_mm512_store_pd(C_block_small + 1 * 8 + 6 * N_pad, c16);

_mm512_store_pd(C_block_small + 1 * 8 + 7 * N_pad, c17);

_mm512_store_pd(C_block_small + 2 * 8 + 0 * N_pad, c20);

_mm512_store_pd(C_block_small + 2 * 8 + 1 * N_pad, c21);

_mm512_store_pd(C_block_small + 2 * 8 + 2 * N_pad, c22);

_mm512_store_pd(C_block_small + 2 * 8 + 3 * N_pad, c23);

_mm512_store_pd(C_block_small + 2 * 8 + 4 * N_pad, c24);

_mm512_store_pd(C_block_small + 2 * 8 + 5 * N_pad, c25);

_mm512_store_pd(C_block_small + 2 * 8 + 6 * N_pad, c26);

_mm512_store_pd(C_block_small + 2 * 8 + 7 * N_pad, c27);

//[[[end]]]
                    }
                }
            }
        }
    }

    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

void square_dgemm_jki_block_jki(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}
void square_dgemm_kji_block_jki(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int k = 0; k < N_pad; k += BLOCK_SIZE){
        for(int j = 0; j < N_pad; j += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

void square_dgemm_jik_block_jki(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int i = 0; i < N_pad; i += BLOCK_SIZE){
            for(int k = 0; k < N_pad; k += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

void square_dgemm_ikj_block_jki(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int i = 0; i < N_pad; i += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int j = 0; j < N_pad; j += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

#define BI(i) ((i) / BLOCK_SIZE)
#define BJ(j) ((j) / BLOCK_SIZE)
#define BII(i) ((i) % BLOCK_SIZE)
#define BJJ(j) ((j) % BLOCK_SIZE)

void cpy_and_pack(int N_pad, int N, double *from, double *to){
    int griddim = N_pad / BLOCK_SIZE;
    for(int j = 0; j < N; ++j){
        for(int i = 0; i < N; ++i){
            to[(BI(i) + BI(j) * griddim) * BLOCK_SIZE * BLOCK_SIZE + BII(i) + BJJ(j) * BLOCK_SIZE] = from[i + j * N];
        }
        for(int i = N; i < N_pad; ++i){
            to[(BI(i) + BI(j) * griddim) * BLOCK_SIZE * BLOCK_SIZE + BII(i) + BJJ(j) * BLOCK_SIZE] = 0;
        }
    }
    for(int j = N; j < N_pad; ++j){
        for(int i = 0; i < N_pad; ++i){
            to[(BI(i) + BI(j) * griddim) * BLOCK_SIZE * BLOCK_SIZE + BII(i) + BJJ(j) * BLOCK_SIZE] = 0;
        }
    }
}

void square_dgemm_jki_block_jki_packing(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    // only pack A because A will be visited O(griddim * n^2)
    cpy_and_pack(N_pad, N, A, A_align);
    // because the whole B array will only be visited exactly once
    // packing will probably not be benefitcial at all
    cpy(N_pad, N, B, B_align);
    cpy_and_pack(N_pad, N, C, C_align);

    int griddim = N_pad / BLOCK_SIZE;

    for(int j = 0; j < griddim; ++j){
        for(int k = 0; k < griddim; ++k){
            for(int i = 0; i < griddim; ++i){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + (i + k * griddim) * BLOCK_SIZE * BLOCK_SIZE;
                double *B_block = B_align + (k * BLOCK_SIZE) + (j * BLOCK_SIZE) * N_pad;
                double *C_block = C_align + (i + j * griddim) * BLOCK_SIZE * BLOCK_SIZE;

                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * BLOCK_SIZE;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * BLOCK_SIZE;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[(BI(i) + BI(j) * griddim) * BLOCK_SIZE * BLOCK_SIZE + BII(i) + BJJ(j) * BLOCK_SIZE];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

#define UNROLLSTEP 6

void square_dgemm_jki_block_jki_unroll(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

/*[[[cog
import cog

UNROLLSTEP = 6
CACHELINE = 8

# define
cog.out(
    """
                            __m512d b;
                            b = _mm512_set1_pd(B_element[0]);
    """
)
for i in range(UNROLLSTEP):
    cog.out(
    """
    """.format(i=i))
]]]*/

__m512d b;
b = _mm512_set1_pd(B_element[0]);
    
    
    
    
    
    
    
//[[[end]]]

                        for(int ii = 0; ii < BLOCK_SIZE; ii += UNROLLSTEP * CACHELINE){
/*[[[cog
import cog

UNROLLSTEP = 6
CACHELINE = 8

# define
# for i in range(UNROLLSTEP):
#     cog.out(
#     """
#                             __m512d b{i};
#                             b{i} = _mm512_set1_pd(B_element[0]);
#     """.format(i=i))

for i in range(UNROLLSTEP):
    cog.out(
    """
                            __m512d a{i};
                            __m512d c{i};
    """.format(i=i))

for i in range(UNROLLSTEP):
    cog.out(
    """
                            a{i} = _mm512_load_pd(A_col + ii + {i} * CACHELINE);
                            c{i} = _mm512_load_pd(C_col + ii + {i} * CACHELINE);
                            c{i} = _mm512_fmadd_pd(a{i}, b, c{i});
                            _mm512_store_pd(C_col + ii + {i} * CACHELINE, c{i});
    """.format(i=i))

]]]*/

__m512d a0;
__m512d c0;
    
__m512d a1;
__m512d c1;
    
__m512d a2;
__m512d c2;
    
__m512d a3;
__m512d c3;
    
__m512d a4;
__m512d c4;
    
__m512d a5;
__m512d c5;
    
a0 = _mm512_load_pd(A_col + ii + 0 * CACHELINE);
c0 = _mm512_load_pd(C_col + ii + 0 * CACHELINE);
c0 = _mm512_fmadd_pd(a0, b, c0);
_mm512_store_pd(C_col + ii + 0 * CACHELINE, c0);
    
a1 = _mm512_load_pd(A_col + ii + 1 * CACHELINE);
c1 = _mm512_load_pd(C_col + ii + 1 * CACHELINE);
c1 = _mm512_fmadd_pd(a1, b, c1);
_mm512_store_pd(C_col + ii + 1 * CACHELINE, c1);
    
a2 = _mm512_load_pd(A_col + ii + 2 * CACHELINE);
c2 = _mm512_load_pd(C_col + ii + 2 * CACHELINE);
c2 = _mm512_fmadd_pd(a2, b, c2);
_mm512_store_pd(C_col + ii + 2 * CACHELINE, c2);
    
a3 = _mm512_load_pd(A_col + ii + 3 * CACHELINE);
c3 = _mm512_load_pd(C_col + ii + 3 * CACHELINE);
c3 = _mm512_fmadd_pd(a3, b, c3);
_mm512_store_pd(C_col + ii + 3 * CACHELINE, c3);
    
a4 = _mm512_load_pd(A_col + ii + 4 * CACHELINE);
c4 = _mm512_load_pd(C_col + ii + 4 * CACHELINE);
c4 = _mm512_fmadd_pd(a4, b, c4);
_mm512_store_pd(C_col + ii + 4 * CACHELINE, c4);
    
a5 = _mm512_load_pd(A_col + ii + 5 * CACHELINE);
c5 = _mm512_load_pd(C_col + ii + 5 * CACHELINE);
c5 = _mm512_fmadd_pd(a5, b, c5);
_mm512_store_pd(C_col + ii + 5 * CACHELINE, c5);
    
//[[[end]]]
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

#undef BLOCK_SIZE
// aim for L2
#define BLOCK_SIZE 160
// aim for L1
#define BLOCK_SIZE2 48

#if EXPERIMENT == 9
const char* dgemm_desc = "Blocking experiment: " STRINGIFY(EXPERIMENT) ", block_size: " STRINGIFY(BLOCK_SIZE) ", block_size2: " STRINGIFY(BLOCK_SIZE2);
#endif

void square_dgemm_jki_block_jki_two_level(int N, double* A, double* B, double* C) {
    assert(BLOCK_SIZE % BLOCK_SIZE2 == 0);

    int N_pad = (N + BLOCK_SIZE2 - 1) / BLOCK_SIZE2 * BLOCK_SIZE2;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                int I = min(N_pad - i, BLOCK_SIZE);
                int J = min(N_pad - j, BLOCK_SIZE);
                int K = min(N_pad - k, BLOCK_SIZE);
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                for(int jj = 0; jj < J; jj += BLOCK_SIZE2){
                    for(int kk = 0; kk < K; kk += BLOCK_SIZE2){
                        for(int ii = 0; ii < I; ii += BLOCK_SIZE2){
                            double *A_block2 = A_block + ii + kk * N_pad;
                            double *B_block2 = B_block + kk + jj * N_pad;
                            double *C_block2 = C_block + ii + jj * N_pad;


                            for(int jjj = 0; jjj < BLOCK_SIZE2; ++jjj){
                                double *B_col = B_block2 + jjj * N_pad;
                                double *C_col = C_block2 + jjj * N_pad;

                                for(int kkk = 0; kkk < BLOCK_SIZE2; ++kkk){
                                    double *A_col = A_block2 + kkk * N_pad;
                                    double *B_element = B_col + kkk;

                                    for(int iii = 0; iii < BLOCK_SIZE2; iii += CACHELINE){
                                        __m512d a,b,c;
                                        a = _mm512_load_pd(A_col + iii);
                                        b = _mm512_set1_pd(B_element[0]);
                                        c = _mm512_load_pd(C_col + iii);
                                        c = _mm512_fmadd_pd(a, b, c);
                                        _mm512_store_pd(C_col + iii, c);
                                    }
                                }
                            }

                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}
#undef BLOCK_SIZE
#define BLOCK_SIZE 48

#define PREFETCH_DIST 8

void square_dgemm_jki_block_jki_prefetch(int N, double* A, double* B, double* C) {
    int N_pad = (N + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < BLOCK_SIZE; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < BLOCK_SIZE; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        _mm_prefetch(A_col + PREFETCH_DIST * N_pad, _MM_HINT_T0);
                        _mm_prefetch(C_col + PREFETCH_DIST * N_pad, _MM_HINT_T0);

                        for(int ii = 0; ii < BLOCK_SIZE; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}
//----------------------------------------
void square_dgemm_jki_block_jki_nopad(int N, double* A, double* B, double* C) {
    int N_pad = (N + CACHELINE - 1) / CACHELINE * CACHELINE;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int j = 0; j < N_pad; j += BLOCK_SIZE){
        int J = min(BLOCK_SIZE, N_pad - j);

        for(int k = 0; k < N_pad; k += BLOCK_SIZE){
            int K = min(BLOCK_SIZE, N_pad - k);
            for(int i = 0; i < N_pad; i += BLOCK_SIZE){
                // read: A_align[i:i+BLOCK_SIZE][k:k+BLOCK_SIZE]
                // read: B_align[k:k+BLOCK_SIZE][j:j+BLOCK_SIZE]
                // read&write: C_align[i:i+BLOCK_SIZE][j:j+BLOCK_SIZE]
                //
                // (I x K) x (K x J)
                int I = min(BLOCK_SIZE, N_pad - i);

                double *A_block = A_align + i + k * N_pad;
                double *B_block = B_align + k + j * N_pad;
                double *C_block = C_align + i + j * N_pad;
                __m512d a,b,c;
                for(int jj = 0; jj < J; ++jj){
                    double *B_col = B_block + jj * N_pad;
                    double *C_col = C_block + jj * N_pad;

                    for(int kk = 0; kk < K; ++kk){
                        double *A_col = A_block + kk * N_pad;
                        double *B_element = B_col + kk;

                        for(int ii = 0; ii < I; ii += CACHELINE){
                            a = _mm512_load_pd(A_col + ii);
                            b = _mm512_set1_pd(B_element[0]);
                            c = _mm512_load_pd(C_col + ii);
                            c = _mm512_fmadd_pd(a, b, c);
                            _mm512_store_pd(C_col + ii, c);
                        }
                    }
                }
            }
        }
    }
    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

#define KC 256
#define MC 256
#define NR 32
#define MR 32
void square_dgemm_gotoblas_block_jki(int N, double* A, double* B, double* C) {
    assert(NR == MR);
    assert(NR % CACHELINE == 0);
    int N_pad = (N + MR - 1) / MR * MR;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int pc = 0; pc < N_pad; pc += KC){
        int K = min(N_pad - pc, KC);
        /* assert(K % NR == 0); */
        for(int ic = 0; ic < N_pad; ic += MC){
            int IR = min(N_pad - ic, MC);
            /* assert(IR % NR == 0); */
            for(int jr = 0; jr < N_pad; jr += NR){
                for(int ir = 0; ir < IR; ir += MR){
                    // A_align[ic+ir:ic+ir+MR][pc:pc+K]
                    // B_align[pc:pc+K][jr:jr+NR]
                    // C_align[ic+ir:ic+ir+MR][jr:jr+NR]
                    double *const A_sliver = A_align + (ic + ir) + (pc) * N_pad;
                    double *const B_sliver = B_align + (pc) + (jr) * N_pad;
                    double *const C_block = C_align + (ic + ir) + (jr) * N_pad;
                    // (MR x K) x (K x NR)
                    __m512d a,b,c;
                    for(int j = 0; j < NR; ++j){
                        double *const B_col = B_sliver + j * N_pad;
                        double *const C_col = C_block + j * N_pad;
                        for(int k = 0; k < K; ++k){
                            double *const B_element = B_col + k;
                            double *const A_col = A_sliver + k * N_pad;
                            for(int i = 0; i < MR; i += CACHELINE){
                                a = _mm512_load_pd(A_col + i);
                                b = _mm512_set1_pd(B_element[0]);
                                c = _mm512_load_pd(C_col + i);
                                c = _mm512_fmadd_pd(a, b, c);
                                _mm512_store_pd(C_col + i, c);
                            }
                        }
                    }
                }
            }
        }
    }

    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}
void square_dgemm_gotoblas_block_kji(int N, double* A, double* B, double* C) {
    assert(NR == MR);
    assert(NR % CACHELINE == 0);
    int N_pad = (N + MR - 1) / MR * MR;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    for(int pc = 0; pc < N_pad; pc += KC){
        int K = min(N_pad - pc, KC);
        /* assert(K % NR == 0); */
        for(int ic = 0; ic < N_pad; ic += MC){
            int IR = min(N_pad - ic, MC);
            /* assert(IR % NR == 0); */
            for(int jr = 0; jr < N_pad; jr += NR){
                for(int ir = 0; ir < IR; ir += MR){
                    // A_align[ic+ir:ic+ir+MR][pc:pc+K]
                    // B_align[pc:pc+K][jr:jr+NR]
                    // C_align[ic+ir:ic+ir+MR][jr:jr+NR]
                    double *const A_sliver = A_align + (ic + ir) + (pc) * N_pad;
                    double *const B_sliver = B_align + (pc) + (jr) * N_pad;
                    double *const C_block = C_align + (ic + ir) + (jr) * N_pad;
                    // (MR x K) x (K x NR)
                    __m512d a,b,c;
                    for(int k = 0; k < K; ++k){
                        double *const B_row = B_sliver + k;
                        double *const A_col = A_sliver + k * N_pad;

                        for(int j = 0; j < NR; ++j){
                            double *const C_col = C_block + j * N_pad;
                            double *const B_element = B_row + j * N_pad;

                            for(int i = 0; i < MR; i += CACHELINE){
                                a = _mm512_load_pd(A_col + i);
                                b = _mm512_set1_pd(B_element[0]);
                                c = _mm512_load_pd(C_col + i);
                                c = _mm512_fmadd_pd(a, b, c);
                                _mm512_store_pd(C_col + i, c);
                            }
                        }
                    }
                }
            }
        }
    }

    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
}

// Check https://www.cs.utexas.edu/users/flame/pubs/blis3_ipdps14.pdf
void square_dgemm_gotoblas_block_kji_packing(int N, double* A, double* B, double* C) {
    assert(NR == MR);
    assert(NR % CACHELINE == 0);
    int N_pad = (N + MR - 1) / MR * MR;

    double *A_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *B_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));
    double *C_align = (double *)_mm_malloc(N_pad * N_pad * sizeof(double), CACHELINE * sizeof(double));

    cpy(N_pad, N, A, A_align);
    cpy(N_pad, N, B, B_align);
    cpy(N_pad, N, C, C_align);

    double *A_pack = (double *)_mm_malloc(MC * KC * sizeof(double), CACHELINE * sizeof(double));
    double *B_pack = (double *)_mm_malloc(KC * N_pad * sizeof(double), CACHELINE * sizeof(double));

    for(int pc = 0; pc < N_pad; pc += KC){
        int K = min(N_pad - pc, KC);
        for(int ic = 0; ic < N_pad; ic += MC){
            int IR = min(N_pad - ic, MC);

            // pack A
            for(int j = 0; j < K; ++j){
                for(int i = 0; i < IR; ++i){
                    A_pack[(i / MR) * MR * K + (i % MR) + j * MR] = A_align[(ic + i) + (pc + j) * N_pad];
                }
            }
            // pack B
            for(int j = 0; j < N_pad; ++j){
                for(int i = 0; i < K; ++i){
                    B_pack[(j / NR) * K * NR + (i * NR + j % NR)] = B_align[(pc + i) + (j) * N_pad];
                }
            }

            for(int jr = 0; jr < N_pad; jr += NR){
                for(int ir = 0; ir < IR; ir += MR){
                    // A_align[ic+ir:ic+ir+MR][pc:pc+K]
                    // B_align[pc:pc+K][jr:jr+NR]
                    // C_align[ic+ir:ic+ir+MR][jr:jr+NR]

                    double *const A_sliver = A_pack + (ir / MR) * MR * K;
                    double *const B_sliver = B_pack + (jr / NR) * K * NR;
                    double *const C_block = C_align + (ic + ir) + (jr) * N_pad;
                    // (MR x K) x (K x NR)
                    __m512d a,b,c;
                    for(int k = 0; k < K; ++k){
                        double *const B_row = B_sliver + k * NR;
                        double *const A_col = A_sliver + k * MR;

                        for(int j = 0; j < NR; ++j){
                            double *const C_col = C_block + j * N_pad;
                            double *const B_element = B_row + j;

                            for(int i = 0; i < MR; i += CACHELINE){
                                a = _mm512_load_pd(A_col + i);
                                b = _mm512_set1_pd(B_element[0]);
                                c = _mm512_load_pd(C_col + i);
                                c = _mm512_fmadd_pd(a, b, c);
                                _mm512_store_pd(C_col + i, c);
                            }
                        }
                    }
                }
            }
        }
    }

    // put back
    for(int j = 0; j < N; j++){
        for(int i = 0; i < N; i++){
            C[i + j * N] = C_align[i + j * N_pad];
        }
    }

    _mm_free(A_align);
    _mm_free(B_align);
    _mm_free(C_align);
    _mm_free(A_pack);
    _mm_free(B_pack);
}
//
// entry point
void square_dgemm(int N, double* A, double* B, double* C) {
#if EXPERIMENT == 0
    square_dgemm_starter_code_modified(N, A, B, C);
#elif EXPERIMENT == 1
    // about 19%
    square_dgemm_block_jki(N, A, B, C);
#elif EXPERIMENT == 2
    // about 12%
    square_dgemm_block_kji(N, A, B, C);
#elif EXPERIMENT == 3
    // about 22% -> 26%(BLOCK_SIZE = 48)
    square_dgemm_jki_block_jki(N, A, B, C);
#elif EXPERIMENT == 4
    // about 20.5%
    square_dgemm_kji_block_jki(N, A, B, C);
#elif EXPERIMENT == 5
    // about 20.49%
    square_dgemm_jik_block_jki(N, A, B, C);
#elif EXPERIMENT == 6
    // about 17.86%
    square_dgemm_ikj_block_jki(N, A, B, C);
#elif EXPERIMENT == 7
    // about 20.8%
    square_dgemm_jki_block_jki_packing(N, A, B, C);
#elif EXPERIMENT == 8
    // about 22% -> 26.44(BLOCK_SIZE = 48)
    square_dgemm_jki_block_jki_unroll(N, A, B, C);
#elif EXPERIMENT == 9
    // about 20% (weird...)
    square_dgemm_jki_block_jki_two_level(N, A, B, C);
#elif EXPERIMENT == 10
    // about 19%
    square_dgemm_jki_block_jki_prefetch(N, A, B, C);
#elif EXPERIMENT == 11
    // about 9.51%
    square_dgemm_jki_block_jki_nopad(N, A, B, C);
#elif EXPERIMENT == 12
    // about 18%
    square_dgemm_gotoblas_block_jki(N, A, B, C);
#elif EXPERIMENT == 13
    // about 13%
    square_dgemm_gotoblas_block_kji(N, A, B, C);
#elif EXPERIMENT == 14
    // about 13%
    square_dgemm_gotoblas_block_kji_packing(N, A, B, C);
#elif EXPERIMENT == 15
    square_dgemm_microkernel(N, A, B, C);
#elif EXPERIMENT == 16
    square_dgemm_jki_microkernel(N, A, B, C);
#else
    assert(0);
#endif
}
