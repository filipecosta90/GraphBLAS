//------------------------------------------------------------------------------
// GxB_Matrix_export_HyperCSR: export a matrix in hypersparse CSR format
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

#include "GB_export.h"

#define GB_FREE_ALL ;

GrB_Info GxB_Matrix_export_HyperCSR  // export and free a hypersparse CSR matrix
(
    GrB_Matrix *A,      // handle of matrix to export and free
    GrB_Type *type,     // type of matrix exported
    GrB_Index *nrows,   // number of rows of the matrix
    GrB_Index *ncols,   // number of columns of the matrix

    GrB_Index **Ap,     // row "pointers", Ap_size >= nvec+1
    GrB_Index **Ah,     // row indices, Ah_size >= nvec
    GrB_Index **Aj,     // column indices, Aj_size >= nvals(A)
    void **Ax,          // values, Ax_size 1, or >= nvals(A)
    GrB_Index *Ap_size, // size of Ap
    GrB_Index *Ah_size, // size of Ah
    GrB_Index *Aj_size, // size of Aj
    GrB_Index *Ax_size, // size of Ax

    GrB_Index *nvec,    // number of rows that appear in Ah
    bool *jumbled,      // if true, indices in each row may be unsorted
    const GrB_Descriptor desc
)
{ 

    //--------------------------------------------------------------------------
    // check inputs and get the descriptor
    //--------------------------------------------------------------------------

    GB_WHERE1 ("GxB_Matrix_export_HyperCSR (&A, &type, &nrows, &ncols, "
        "&Ap, &Ah, &Aj, &Ax, &Ap_size, &Ah_size, &Aj_size, &Ax_size, "
        "&nvec, &jumbled, desc)") ;
    GB_BURBLE_START ("GxB_Matrix_export_HyperCSR") ;
    GB_RETURN_IF_NULL (A) ;
    GB_RETURN_IF_NULL_OR_FAULTY (*A) ;
    GB_GET_DESCRIPTOR (info, desc, xx1, xx2, xx3, xx4, xx5, xx6) ;

    //--------------------------------------------------------------------------
    // finish any pending work
    //--------------------------------------------------------------------------

    if (jumbled == NULL)
    { 
        // the exported matrix cannot be jumbled
        GB_MATRIX_WAIT (*A) ;
    }
    else
    { 
        // the exported matrix is allowed to be jumbled
        GB_MATRIX_WAIT_IF_PENDING_OR_ZOMBIES (*A) ;
    }

    //--------------------------------------------------------------------------
    // ensure the matrix is hypersparse CSR
    //--------------------------------------------------------------------------

    // ensure the matrix is in CSR format
    if ((*A)->is_csc)
    { 
        // A = A', done in-place, to put A in CSR format
        GBURBLE ("(transpose) ") ;
        GB_OK (GB_transpose (NULL, NULL, false, *A,
            NULL, NULL, NULL, false, Context)) ;
    }

    GB_OK (GB_convert_any_to_hyper (*A, Context)) ;
    ASSERT (GB_IS_HYPERSPARSE (*A)) ;

    //--------------------------------------------------------------------------
    // export the matrix
    //--------------------------------------------------------------------------

    int sparsity ;
    bool is_csc ;

    info = GB_export (A, type, ncols, nrows,
        Ap,   Ap_size,  // Ap
        Ah,   Ah_size,  // Ah
        NULL, NULL,     // Ab
        Aj,   Aj_size,  // Aj
        Ax,   Ax_size,  // Ax
        NULL, jumbled, nvec,                // jumbled or not
        &sparsity, &is_csc, Context) ;      // hypersparse by row

    if (info == GrB_SUCCESS)
    {
        ASSERT (sparsity == GxB_HYPERSPARSE) ;
        ASSERT (!is_csc) ;
    }
    GB_BURBLE_END ;
    return (info) ;
}

