//------------------------------------------------------------------------------
// GB_subassign_method6a: C(I,J)<!M> += A ; no S
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// Method 6a: C(I,J)<!M> += A ; no S

// M:           present
// Mask_comp:   true
// C_replace:   false
// accum:       present
// A:           matrix
// S:           none (see also Method 14b)

// Compare with Method 14b, which computes the same thing, but creates S first.

#include "GB_subassign.h"

GrB_Info GB_subassign_method6a
(
    GrB_Matrix C,
    // input:
    const GrB_Index *I,
    const int64_t nI,
    const int Ikind,
    const int64_t Icolon [3],
    const GrB_Index *J,
    const int64_t nJ,
    const int Jkind,
    const int64_t Jcolon [3],
    const GrB_Matrix M,
    const GrB_BinaryOp accum,
    const GrB_Matrix A,
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // get inputs
    //--------------------------------------------------------------------------

    GB_GET_C ;
    GB_GET_MASK ;
    GB_GET_A ;
    GB_GET_ACCUM ;

    //--------------------------------------------------------------------------
    // Method 6a: C(I,J)<!M> += A ; no S
    //--------------------------------------------------------------------------

    // Time: Close to Optimal.  All entries in A must be traversed, so the
    // time is Omega(nnz(A)).  This method traverses all entries in the single
    // matrix A, and does a binary search for the corresponding entry in M.

    // The sparsity of !M cannot be easily exploited, since !M is essentially
    // dense: M(i,j) present and zero is the same as M(i,j) not present.  As a
    // result, traversing all entries of interest in !M is not feasible, and
    // would take Omega(|I|*|J|) time.  That is too high, since nnz(A) <<
    // |I|*|J| normally holds.

    // For each entry in A to be accumulated into C, a binary search for
    // C(iC,jC) is performed to determine which action to take (insert a new
    // entry into C, or accumulate into a prior one).

    // The total time is thus O(nnz(A)*(log(c)+log(d))+Mnvec) if C standard and
    // not dense, where d = avg nnz M(:,j).  An additional time of
    // O(anvec*log(Cnvec)) is added if C is hypersparse.

    // Method 6a and Method 6b are very similar.

    //--------------------------------------------------------------------------
    // Parallel: slice A into coarse/fine tasks (Method 1, 2, 5, 6a, 6b)
    //--------------------------------------------------------------------------

    GB_SUBASSIGN_1_SLICE (A) ;

    //--------------------------------------------------------------------------
    // phase 1: create zombies, update entries, and count pending tuples
    //--------------------------------------------------------------------------

    #pragma omp parallel for num_threads(nthreads) schedule(dynamic,1) \
        reduction(+:nzombies)
    for (int taskid = 0 ; taskid < ntasks ; taskid++)
    {

        //----------------------------------------------------------------------
        // get the task descriptor
        //----------------------------------------------------------------------

        GB_GET_TASK_DESCRIPTOR ;

        //----------------------------------------------------------------------
        // compute all vectors in this task
        //----------------------------------------------------------------------

        for (int64_t k = kfirst ; k <= klast ; k++)
        {

            //------------------------------------------------------------------
            // get j, the kth vector of A
            //------------------------------------------------------------------

            int64_t j = (Ah == NULL) ? k : Ah [k] ;
            GB_GET_VECTOR (pA, pA_end, pA, pA_end, Ap, k) ;
            int64_t ajnz = pA_end - pA ;
            if (ajnz == 0) continue ;

            //------------------------------------------------------------------
            // get jC, the corresponding vector of C
            //------------------------------------------------------------------

            GB_GET_jC ;

            //------------------------------------------------------------------
            // get M(:,j)
            //------------------------------------------------------------------

            int64_t pM_start, pM_end ;
            GB_VECTOR_LOOKUP (pM_start, pM_end, M, j) ;

            //------------------------------------------------------------------
            // C(I,jC)<!M(:,j)> += A(:,j) ; no S
            //------------------------------------------------------------------

            if (pC_end - pC_start == cvlen)
            {

                //--------------------------------------------------------------
                // C(:,jC) is dense so binary search of C is not needed
                //--------------------------------------------------------------

                for ( ; pA < pA_end ; pA++)
                {

                    //----------------------------------------------------------
                    // consider the entry A(iA,j)
                    //----------------------------------------------------------

                    int64_t iA = Ai [pA] ;

                    //----------------------------------------------------------
                    // find C(iC,jC), but only if M(iA,j) allows it
                    //----------------------------------------------------------

                    GB_MIJ_BINARY_SEARCH (iA) ;
                    mij = !mij ;
                    if (mij)
                    { 

                        //------------------------------------------------------
                        // C(iC,jC) += A(iA,j)
                        //------------------------------------------------------

                        // direct lookup of C(iC,jC)
                        GB_iC_DENSE_LOOKUP ;

                        // ----[C A 1] or [X A 1]-------------------------------
                        // [C A 1]: action: ( =C+A ): apply accum
                        // [X A 1]: action: ( undelete ): zombie live
                        GB_withaccum_C_A_1_matrix ;
                    }
                }

            }
            else
            {

                //--------------------------------------------------------------
                // C(:,jC) is sparse; use binary search for C
                //--------------------------------------------------------------

                for ( ; pA < pA_end ; pA++)
                {

                    //----------------------------------------------------------
                    // consider the entry A(iA,j)
                    //----------------------------------------------------------

                    int64_t iA = Ai [pA] ;

                    //----------------------------------------------------------
                    // find C(iC,jC), but only if M(iA,j) allows it
                    //----------------------------------------------------------

                    GB_MIJ_BINARY_SEARCH (iA) ;
                    mij = !mij ;
                    if (mij)
                    {

                        //------------------------------------------------------
                        // C(iC,jC) += A(iA,j)
                        //------------------------------------------------------

                        // binary search for C(iC,jC) in C(:,jC)
                        GB_iC_BINARY_SEARCH ;

                        if (found)
                        { 
                            // ----[C A 1] or [X A 1]---------------------------
                            // [C A 1]: action: ( =C+A ): apply accum
                            // [X A 1]: action: ( undelete ): zombie lives
                            GB_withaccum_C_A_1_matrix ;
                        }
                        else
                        { 
                            // ----[. A 1]--------------------------------------
                            // action: ( insert )
                            task_pending++ ;
                        }
                    }
                }
            }
        }

        GB_PHASE1_TASK_WRAPUP ;
    }

    //--------------------------------------------------------------------------
    // phase 2: insert pending tuples
    //--------------------------------------------------------------------------

    GB_PENDING_CUMSUM ;

    #pragma omp parallel for num_threads(nthreads) schedule(dynamic,1) \
        reduction(&&:pending_sorted)
    for (int taskid = 0 ; taskid < ntasks ; taskid++)
    {

        //----------------------------------------------------------------------
        // get the task descriptor
        //----------------------------------------------------------------------

        GB_GET_TASK_DESCRIPTOR ;
        GB_START_PENDING_INSERTION ;

        //----------------------------------------------------------------------
        // compute all vectors in this task
        //----------------------------------------------------------------------

        for (int64_t k = kfirst ; k <= klast ; k++)
        {

            //------------------------------------------------------------------
            // get j, the kth vector of A
            //------------------------------------------------------------------

            int64_t j = (Ah == NULL) ? k : Ah [k] ;
            GB_GET_VECTOR (pA, pA_end, pA, pA_end, Ap, k) ;
            int64_t ajnz = pA_end - pA ;
            if (ajnz == 0) continue ;

            //------------------------------------------------------------------
            // get jC, the corresponding vector of C
            //------------------------------------------------------------------

            GB_GET_jC ;

            //------------------------------------------------------------------
            // get M(:,j)
            //------------------------------------------------------------------

            int64_t pM_start, pM_end ;
            GB_VECTOR_LOOKUP (pM_start, pM_end, M, j) ;

            //------------------------------------------------------------------
            // C(I,jC)<!M(:,j)> += A(:,j) ; no S
            //------------------------------------------------------------------

            if (pC_end - pC_start != cvlen)
            {

                //--------------------------------------------------------------
                // C(:,jC) is sparse; use binary search for C
                //--------------------------------------------------------------

                for ( ; pA < pA_end ; pA++)
                {

                    //----------------------------------------------------------
                    // consider the entry A(iA,j)
                    //----------------------------------------------------------

                    int64_t iA = Ai [pA] ;

                    //----------------------------------------------------------
                    // find C(iC,jC), but only if M(iA,j) allows it
                    //----------------------------------------------------------

                    GB_MIJ_BINARY_SEARCH (iA) ;
                    mij = !mij ;
                    if (mij)
                    {

                        //------------------------------------------------------
                        // C(iC,jC) += A(iA,j)
                        //------------------------------------------------------

                        // binary search for C(iC,jC) in C(:,jC)
                        GB_iC_BINARY_SEARCH ;

                        if (!found)
                        { 
                            // ----[. A 1]--------------------------------------
                            // action: ( insert )
                            GB_PENDING_INSERT (Ax +(pA*asize)) ;
                        }
                    }
                }
            }
        }

        GB_PHASE2_TASK_WRAPUP ;
    }

    //--------------------------------------------------------------------------
    // finalize the matrix and return result
    //--------------------------------------------------------------------------

    GB_SUBASSIGN_WRAPUP ;
}
