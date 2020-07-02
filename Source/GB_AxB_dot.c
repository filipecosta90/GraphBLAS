//------------------------------------------------------------------------------
// GB_AxB_dot: C<M>=A'*B using dot products
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// Parallel matrix-matrix multiply, A'*B, with optional mask M.  This
// method is used by GrB_mxm, GrB_vxm, and GrB_mxv.  For both of the latter two
// methods, B on input will be an nrows-by-1 column vxector.

// This function, and the matrices C, M, A, and B are all CSR/CSC agnostic.
// For this discussion, suppose they are CSC, with vlen = # of rows, and vdim =
// # of columns.

// C=A'*B, C<M>=A'*B or C<!M>=A'*B is being computed.  A has not been
// transposed yet (and will not be).  A and B must have the same vector length,
// vlen (as if both A and B are CSC matrices with the same number of rows, for
// example).  GB_AxB_dot2 and GB_AxB_dot3 operate on A' without forming it.
// GB_AxB_dot2 computes C=A'*B and C<!M>=A'*B, and it takes Omega(m*n) time,
// if C is m-by-n.  It is thus only suitable for cases when A and B are large,
// and C is small.  GB_AxB_dot3 computes C<M>=A'*B, and it only needs to
// examine entries in M, taking Omega(nnz(M)) time.  It can thus be used for
// very large matrices C.  GB_AxB_dot4 computes C+=A'*B when C is dense.

// The output matrix C = *Chandle has not been allocated, so C is NULL on
// input.  The mask M is optional.

// If C is computed in place, Chandle is ignored, and the result is computed
// in C_in_place instead.  This case requires the accum operator to match
// the monoid of the semiring.

// The semiring defines C=A*B.  flipxy modifies how the semiring multiply
// operator is applied.  If false, then fmult(aik,bkj) is computed.  If true,
// then the operands are swapped, and fmult(bkj,aij) is done instead.

// Context: the GB_Context containing the # of threads to use, a string of the
// user-callable function that is calling this function (GrB_mxm, GrB_mxv, or
// GxB_vxm) and detailed error reports.

#include "GB_mxm.h"
#define GB_FREE_ALL ;

GrB_Info GB_AxB_dot                 // dot product (multiple methods)
(
    GrB_Matrix *Chandle,            // output matrix, NULL on input
    GrB_Matrix C_in_place,          // input/output matrix, if done in place
    GrB_Matrix M,                   // optional mask matrix
    const bool Mask_comp,           // if true, use !M
    const bool Mask_struct,         // if true, use the only structure of M
    const GrB_Matrix A,             // input matrix A
    const GrB_Matrix B,             // input matrix B
    const GrB_Semiring semiring,    // semiring that defines C=A*B
    const bool flipxy,              // if true, do z=fmult(b,a) vs fmult(a,b)
    bool *mask_applied,             // if true, mask was applied
    bool *done_in_place,            // if true, C_in_place was computed in place
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    ASSERT (Chandle != NULL) ;          // C = (*Chandle) is NULL
    ASSERT (*Chandle == NULL) ;
    ASSERT_MATRIX_OK_OR_NULL (M, "M for parallel A*B", GB0) ;
    ASSERT_MATRIX_OK (A, "A for parallel A*B", GB0) ;
    ASSERT_MATRIX_OK (B, "B for parallel A*B", GB0) ;
    ASSERT (!GB_PENDING (M)) ; ASSERT (!GB_ZOMBIES (M)) ;
    ASSERT (!GB_PENDING (A)) ; ASSERT (!GB_ZOMBIES (A)) ;
    ASSERT (!GB_PENDING (B)) ; ASSERT (!GB_ZOMBIES (B)) ;
    ASSERT_SEMIRING_OK (semiring, "semiring for parallel A*B", GB0) ;

    int64_t naslice = 0 ;
    int64_t nbslice = 0 ;

    if (M != NULL && !Mask_comp)
    { 

        //======================================================================
        // C<M>=A'*B
        //======================================================================

        // use dot3 if M is present and not complemented
        GBBURBLE ("dot3 ") ;
        (*mask_applied) = true ;

        #if defined ( GBCUDA )

        // very rough estimate of the work to do
        double adeg = ((double) GB_NNZ (A)) / ((double) GB_IMAX (1, A->nvec)) ;
        double bdeg = ((double) GB_NNZ (B)) / ((double) GB_IMAX (1, B->nvec)) ;
        double work = GB_NNZ (M) * GB_IMIN (adeg, bdeg) ;

        // TODO for GPU: if A or B are not accessed (first, 2nd, or pair
        // ops) then the type of A can be user-defined here, for CUDA.

        int ngpus_to_use = GB_ngpus_to_use (work) ;
        if (ngpus_to_use > 0 && semiring->builtin &&
            && (A->type->code != GB_UDT_code)
            && (B->type->code != GB_UDT_code))
        {
            // use "the" GPU (TODO for GPU: could use multiple GPUs too)
            return (GB_AxB_dot3_cuda (Chandle, M, Mask_struct, A, B, semiring,
                flipxy, Context)) ;
        }
        else
        #endif
        {
            // use the CPU
            return (GB_AxB_dot3 (Chandle, M, Mask_struct, A, B, semiring,
                flipxy, Context)) ;
        }

    }
    else
    {

        //======================================================================
        // C<!M>=A'*B or C=A'*B
        //======================================================================

        GrB_Info info ;

        //----------------------------------------------------------------------
        // get A and B
        //----------------------------------------------------------------------

        if (B->nvec_nonempty < 0)
        { 
            B->nvec_nonempty = GB_nvec_nonempty (B, NULL) ;
        }

        if (A->nvec_nonempty < 0)
        { 
            A->nvec_nonempty = GB_nvec_nonempty (A, NULL) ;
        }

        //======================================================================
        // in place C+=A'*B
        //======================================================================

        if (C_in_place != NULL && M == NULL && !Mask_comp)
        { 
            GBBURBLE ("dense, C+=A'*B in place ") ;
            (*done_in_place) = true ;
            return (GB_AxB_dot4 (C_in_place, A, B, semiring, flipxy, Context)) ;
        }

        //----------------------------------------------------------------------
        // determine the number of threads to use
        //----------------------------------------------------------------------

        int64_t anvec = A->nvec ;
        int64_t anz   = GB_NNZ (A) ;

        int64_t bnvec = B->nvec ;
        int64_t bnz   = GB_NNZ (B) ;

        ASSERT (A->vlen == B->vlen) ;

        GB_GET_NTHREADS_MAX (nthreads_max, chunk, Context) ;
        int nthreads = GB_nthreads (anz + bnz, chunk, nthreads_max) ;

        if (nthreads == 1)
        {
            // do the entire computation with a single thread
            naslice = 1 ;
            nbslice = 1 ;
        }
        else
        {
            // determine number of slices for A' and B
            if (bnvec > 32 * nthreads || bnvec == 0)
            { 
                // just slice B
                nbslice = 32 * nthreads ;
                naslice = 1 ;
            }
            else
            { 
                // slice B into individual vectors
                nbslice = bnvec ;

                // slice A' to get a total of about 32*nthreads tasks
                naslice = (32 * nthreads) / nbslice ;

                // but do not slice A too finely
                naslice = GB_IMIN (naslice, anvec/4) ;
                naslice = GB_IMAX (naslice, nthreads) ;
            }
        }

        //----------------------------------------------------------------------
        // C = A'*B or C<!M> = A'*B
        //----------------------------------------------------------------------

        GB_OK (GB_AxB_dot2 (Chandle, M, Mask_struct, A, B, semiring,
            flipxy, mask_applied, nthreads, naslice, nbslice, Context)) ;

        //----------------------------------------------------------------------
        // return result
        //----------------------------------------------------------------------

        ASSERT_MATRIX_OK (*Chandle, "C for dot2 A'*B", GB0) ;
        return (GrB_SUCCESS) ;
    }
}

