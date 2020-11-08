//------------------------------------------------------------------------------
// GB_ix_resize:  reallocate a matrix with some slack for future growth
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// nnz(A) has, or will, change.  The # of nonzeros may decrease significantly,
// in which case the extra space is trimmed.  If the existing space is not
// sufficient, the matrix is doubled in size to accomodate the new entries.

#include "GB.h"

GrB_Info GB_ix_resize           // resize a matrix
(
    GrB_Matrix A,
    const int64_t anz_new,      // required new nnz(A)
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    // This function is only called by GB_Matrix_wait.  Full and bitmap
    // matrices never have any pending work, so this method is needed only for
    // sparse and hypersparse matrices.
    ASSERT_MATRIX_OK (A, "A to resize", GB0) ;
    ASSERT (!GB_IS_FULL (A)) ;
    ASSERT (!GB_IS_BITMAP (A)) ;
    ASSERT (GB_IS_SPARSE (A) || GB_IS_HYPERSPARSE (A)) ;

    // This function tolerates pending tuples, zombies, and jumbled matrices.
    ASSERT (GB_ZOMBIES_OK (A)) ;
    ASSERT (GB_JUMBLED_OK (A)) ;
    ASSERT (GB_PENDING_OK (A)) ;

    GrB_Info info ;
    int64_t anzmax_orig = A->nzmax ;
    ASSERT (GB_NNZ (A) <= anzmax_orig) ;

    //--------------------------------------------------------------------------
    // resize the matrix
    //--------------------------------------------------------------------------

    if (anz_new < anzmax_orig / 4)
    {   GB_cov[3427]++ ;
// NOT COVERED (3427):

        //----------------------------------------------------------------------
        // shrink the space
        //----------------------------------------------------------------------

        // the new matrix has lots of leftover space.  Trim the size but leave
        // 50% for future growth, if possible.  Do not increase the size beyond
        // the existing space, however.

        int64_t anzmax_new = GB_IMAX (anzmax_orig, anz_new + (anz_new/2)) ;

        // since the space is shrinking, this is guaranteed not to fail
        ASSERT (anzmax_new <= anzmax_orig) ;
        ASSERT (anz_new <= anzmax_new) ;

        info = GB_ix_realloc (A, anzmax_new, true, Context) ;
        ASSERT (info == GrB_SUCCESS) ;
        ASSERT_MATRIX_OK (A, "A trimmed in size", GB0) ;

    }
    else if (anz_new > anzmax_orig)
    {

        //----------------------------------------------------------------------
        // grow the space
        //----------------------------------------------------------------------

        // original A->nzmax is not enough; give the matrix space for nnz(A)
        // plus 50% for future growth

        int64_t anzmax_new = anz_new + (anz_new/2) ;

        // the space is growing so this might run out of memory
        ASSERT (anzmax_new > anzmax_orig) ;
        ASSERT (anz_new <= anzmax_new) ;

        info = GB_ix_realloc (A, anzmax_new, true, Context) ;
        if (info != GrB_SUCCESS)
        {   GB_cov[3428]++ ;
// covered (3428): 7611
            // out of memory
            GB_phbix_free (A) ;
            return (info) ;
        }
        ASSERT_MATRIX_OK (A, "A increased in size", GB0) ;

    }
    else
    {   GB_cov[3429]++ ;
// covered (3429): 140098

        //----------------------------------------------------------------------
        // leave as-is
        //----------------------------------------------------------------------

        // nnz(A) has changed but the old space is enough to use as-is;
        // do nothing
        ASSERT (anz_new <= anzmax_orig) ;
        ASSERT_MATRIX_OK (A, "A left as-is", GB0) ;
    }

    //--------------------------------------------------------------------------
    // return the result
    //--------------------------------------------------------------------------

    ASSERT (anz_new <= A->nzmax) ;
    return (GrB_SUCCESS) ;
}
