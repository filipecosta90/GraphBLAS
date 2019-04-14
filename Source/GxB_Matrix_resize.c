//------------------------------------------------------------------------------
// GxB_Matrix_resize: change the size of a matrix
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

#include "GB.h"

GrB_Info GxB_Matrix_resize      // change the size of a matrix
(
    GrB_Matrix A,               // matrix to modify
    GrB_Index nrows_new,        // new number of rows in matrix
    GrB_Index ncols_new         // new number of columns in matrix
)
{ 

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    GB_WHERE ("GxB_Matrix_resize (A, nrows_new, ncols_new)") ;
    GB_RETURN_IF_NULL_OR_FAULTY (A) ;
    Context->nthreads = GxB_DEFAULT ;   // no descriptor, so use default rule

    //--------------------------------------------------------------------------
    // resize the matrix
    //--------------------------------------------------------------------------

    return (GB_resize (A, nrows_new, ncols_new, Context)) ;
}

