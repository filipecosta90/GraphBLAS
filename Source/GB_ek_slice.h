//------------------------------------------------------------------------------
// GB_ek_slice.h: slice the entries and vectors of a matrix
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// Slice the entries of a matrix or vector into ntasks slices.

// Task t does entries pstart_slice [t] to pstart_slice [t+1]-1 and
// vectors kfirst_slice [t] to klast_slice [t].  The first and last vectors
// may be shared with prior slices and subsequent slices.

// On input, ntasks must be <= nnz (A), unless nnz (A) is zero.  In that
// case, ntasks must be 1.

#include "GB.h"

void GB_ek_slice
(
    // output:
    int64_t *restrict pstart_slice, // size ntasks+1
    int64_t *restrict kfirst_slice, // size ntasks
    int64_t *restrict klast_slice,  // size ntasks
    // input:
    GrB_Matrix A,                   // matrix to slize
    int ntasks                      // # of tasks
) ;

//------------------------------------------------------------------------------
// GB_get_pA_and_pC: find the part of A(:,k) to be operated on by this task
//------------------------------------------------------------------------------

// The tasks were generated by GB_ek_slice.

static inline void GB_get_pA_and_pC
(
    // output
    int64_t *pA_start,
    int64_t *pA_end,
    int64_t *pC,
    // input
    int tid,            // task id
    int64_t k,          // current vector
    int64_t kfirst,     // first vector for this slice
    int64_t klast,      // last vector for this slice
    const int64_t *restrict pstart_slice,   // start of each slice in A
    const int64_t *restrict C_pstart_slice, // start of each slice in C
    const int64_t *restrict Cp,             // vector pointers for C
    const int64_t *restrict Ap              // vector pointers for A
)
{
    if (k == kfirst)
    { 
        // First vector for task tid; may only be partially owned.
        (*pA_start) = pstart_slice [tid] ;
        (*pA_end  ) = GB_IMIN (Ap [kfirst+1], pstart_slice [tid+1]) ;
        if (pC != NULL) (*pC) = C_pstart_slice [tid] ;
    }
    else if (k == klast)
    { 
        // Last vector for task tid; may only be partially owned.
        (*pA_start) = Ap [k] ;
        (*pA_end  ) = pstart_slice [tid+1] ;
        if (pC != NULL) (*pC) = Cp [k] ;
    }
    else
    { 
        // task tid fully owns this vector A(:,k).
        (*pA_start) = Ap [k] ;
        (*pA_end  ) = Ap [k+1] ;
        if (pC != NULL) (*pC) = Cp [k] ;
    }
}

