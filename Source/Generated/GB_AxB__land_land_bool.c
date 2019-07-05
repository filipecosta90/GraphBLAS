//------------------------------------------------------------------------------
// GB_AxB:  hard-coded functions for semiring: C<M>=A*B or A'*B
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// Unless this file is Generator/GB_AxB.c, do not edit it (auto-generated)

#include "GB.h"
#ifndef GBCOMPACT
#include "GB_control.h"
#include "GB_ek_slice.h"
#include "GB_Sauna.h"
#include "GB_AxB__include.h"

// The C=A*B semiring is defined by the following types and operators:

// A*B function (Gustavon):  GB_AgusB__land_land_bool
// A'*B function (dot):      GB_Adot2B__land_land_bool
// A*B function (heap):      GB_AheapB__land_land_bool

// C type:   bool
// A type:   bool
// B type:   bool

// Multiply: z = (aik && bkj)
// Add:      cij = (cij && z)
// MultAdd:  cij = (cij && (aik && bkj))
// Identity: true
// Terminal: if (cij == false) break ;

#define GB_ATYPE \
    bool

#define GB_BTYPE \
    bool

#define GB_CTYPE \
    bool

// aik = Ax [pA]
#define GB_GETA(aik,Ax,pA) \
    bool aik = Ax [pA]

// bkj = Bx [pB]
#define GB_GETB(bkj,Bx,pB) \
    bool bkj = Bx [pB]

#define GB_CX(p) Cx [p]

// multiply operator
#define GB_MULT(z, x, y)        \
    z = (x && y) ;

// multiply-add
#define GB_MULTADD(z, x, y)     \
    z = (z && (x && y)) ;

// copy scalar
#define GB_COPY(z,x) z = x ;

// monoid identity value (Gustavson's method only, with no mask)
#define GB_IDENTITY \
    true

// break if cij reaches the terminal value (dot product only)
#define GB_DOT_TERMINAL(cij) \
    if (cij == false) break ;

// simd pragma for dot product
#define GB_DOT_SIMD \
    ;

// cij is not a pointer but a scalar; nothing to do
#define GB_CIJ_REACQUIRE(cij,cnz) ;

// declare the cij scalar
#define GB_CIJ_DECLARE(cij) ; \
    bool cij ;

// save the value of C(i,j)
#define GB_CIJ_SAVE(cij,p) Cx [p] = cij ;

#define GB_SAUNA_WORK(i) Sauna_Work [i]

// disable this semiring and use the generic case if these conditions hold
#define GB_DISABLE \
    (GxB_NO_LAND || GxB_NO_BOOL || GxB_NO_LAND_BOOL || GxB_NO_LAND_LAND_BOOL)

//------------------------------------------------------------------------------
// C<M>=A*B and C=A*B: gather/scatter saxpy-based method (Gustavson)
//------------------------------------------------------------------------------

GrB_Info GB_AgusB__land_land_bool
(
    GrB_Matrix C,
    const GrB_Matrix M,
    const GrB_Matrix A, bool A_is_pattern,
    const GrB_Matrix B, bool B_is_pattern,
    GB_Sauna Sauna
)
{ 
    #if GB_DISABLE
    return (GrB_NO_VALUE) ;
    #else
    bool *restrict Sauna_Work = Sauna->Sauna_Work ;
    bool *restrict Cx = C->x ;
    GrB_Info info = GrB_SUCCESS ;
    #include "GB_AxB_Gustavson_meta.c"
    return (info) ;
    #endif
}

//------------------------------------------------------------------------------
// C<M>=A'*B, C<!M>=A'*B or C=A'*B: dot product (phase 2)
//------------------------------------------------------------------------------

GrB_Info GB_Adot2B__land_land_bool
(
    GrB_Matrix C,
    const GrB_Matrix M, const bool Mask_comp,
    const GrB_Matrix *Aslice, bool A_is_pattern,
    const GrB_Matrix B, bool B_is_pattern,
    int64_t **C_counts,
    int nthreads, int naslice, int nbslice
)
{ 
    #if GB_DISABLE
    return (GrB_NO_VALUE) ;
    #else
    #define GB_PHASE_2_OF_2
    #include "GB_AxB_dot2_meta.c"
    #undef GB_PHASE_2_OF_2
    return (GrB_SUCCESS) ;
    #endif
}

//------------------------------------------------------------------------------
// C<M>=A*B and C=A*B: heap saxpy-based method
//------------------------------------------------------------------------------

#include "GB_heap.h"

GrB_Info GB_AheapB__land_land_bool
(
    GrB_Matrix *Chandle,
    const GrB_Matrix M,
    const GrB_Matrix A, bool A_is_pattern,
    const GrB_Matrix B, bool B_is_pattern,
    int64_t *restrict List,
    GB_pointer_pair *restrict pA_pair,
    GB_Element *restrict Heap,
    const int64_t bjnz_max
)
{ 
    #if GB_DISABLE
    return (GrB_NO_VALUE) ;
    #else
    GrB_Matrix C = (*Chandle) ;
    bool *restrict Cx = C->x ;
    bool cij ;
    int64_t cvlen = C->vlen ;
    GrB_Info info = GrB_SUCCESS ;
    #include "GB_AxB_heap_meta.c"
    return (info) ;
    #endif
}

#endif

