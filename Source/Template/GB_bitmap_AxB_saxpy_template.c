//------------------------------------------------------------------------------
// GB_bitmap_AxB_saxpy_template.c: C<#M>+=A*B when C is bitmap
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// GB_AxB_saxpy_sparsity determines the sparsity structure for C<M or !M>=A*B
// or C=A*B, and this template is used when C is bitmap.  C is modified
// in-place.  The accum operator is the same as the monoid.

{
// printf ("BITMAP AXB SAXPY #############################################\n") ;

    //--------------------------------------------------------------------------
    // declare workspace
    //--------------------------------------------------------------------------

    int64_t *GB_RESTRICT A_slice = NULL ;
    int64_t *GB_RESTRICT B_slice = NULL ;
    int64_t *GB_RESTRICT pstart_Mslice = NULL ;
    int64_t *GB_RESTRICT kfirst_Mslice = NULL ;
    int64_t *GB_RESTRICT klast_Mslice  = NULL ;

    //--------------------------------------------------------------------------
    // determine max # of threads to use
    //--------------------------------------------------------------------------

    GB_GET_NTHREADS_MAX (nthreads_max, chunk, Context) ;

    //--------------------------------------------------------------------------
    // get C, M, A, and B
    //--------------------------------------------------------------------------

    ASSERT (GB_IS_BITMAP (C)) ;                 // C is always bitmap
    int8_t *GB_RESTRICT Cb = C->b ;
    GB_CTYPE *GB_RESTRICT Cx = (GB_CTYPE *) C->x ;
    const int64_t cvlen = C->vlen ;
    ASSERT (C->vlen == A->vlen) ;
    ASSERT (C->vdim == B->vdim) ;
    ASSERT (A->vdim == B->vlen) ;
    int64_t cnvals = C->nvals ;

    const int64_t *GB_RESTRICT Bp = B->p ;
    const int64_t *GB_RESTRICT Bh = B->h ;
    const int8_t  *GB_RESTRICT Bb = B->b ;
    const int64_t *GB_RESTRICT Bi = B->i ;
    const GB_BTYPE *GB_RESTRICT Bx = (GB_BTYPE *) (B_is_pattern ? NULL : B->x) ;
    const int64_t bvlen = B->vlen ;
    const int64_t bvdim = B->vdim ;
    const int64_t bnvec = B->nvec ;
    const bool B_is_sparse_or_hyper = GB_IS_SPARSE (B) || GB_IS_HYPERSPARSE (B);
    const bool B_jumbled = B->jumbled ;
    const int64_t bnz = GB_NNZ_HELD (B) ;
    const bool B_is_bitmap = GB_IS_BITMAP (B) ;

    const int64_t *GB_RESTRICT Ap = A->p ;
    const int64_t *GB_RESTRICT Ah = A->h ;
    const int8_t  *GB_RESTRICT Ab = A->b ;
    const int64_t *GB_RESTRICT Ai = A->i ;
    const GB_ATYPE *GB_RESTRICT Ax = (GB_ATYPE *) (A_is_pattern ? NULL : A->x) ;
    const int64_t anvec = A->nvec ;
    const int64_t avlen = A->vlen ;
    const int64_t avdim = A->vdim ;
    const bool A_is_hyper = GB_IS_HYPERSPARSE (A) ;
    const bool A_is_bitmap = GB_IS_BITMAP (A) ;
    const bool A_is_sparse_or_hyper = GB_IS_SPARSE (A) || GB_IS_HYPERSPARSE (A);
    const bool A_jumbled = A->jumbled ;
    const int64_t anz = GB_NNZ_HELD (A) ;

    const int64_t *GB_RESTRICT Mp = NULL ;
    const int64_t *GB_RESTRICT Mh = NULL ;
    const int8_t  *GB_RESTRICT Mb = NULL ;
    const int64_t *GB_RESTRICT Mi = NULL ;
    const GB_void *GB_RESTRICT Mx = NULL ;
    size_t msize = 0 ;
    int64_t mnvec = 0 ;
    int64_t mvlen = 0 ;
    const bool M_is_hyper  = GB_IS_HYPERSPARSE (M) ;
    const bool M_is_sparse = GB_IS_SPARSE (M) ;
    const bool M_is_sparse_or_hyper = M_is_hyper || M_is_sparse ;
    const bool M_is_bitmap = GB_IS_BITMAP (M) ;
    const bool M_is_full   = GB_IS_FULL (M) ;
    int64_t mnz = 0 ;
    int mthreads = 0 ;
    int mtasks = 0 ;

    if (M != NULL)
    { 
        ASSERT (C->vlen == M->vlen) ;
        ASSERT (C->vdim == M->vdim) ;
        Mp = M->p ;
        Mh = M->h ;
        Mb = M->b ;
        Mi = M->i ;
        Mx = (GB_void *) (Mask_struct ? NULL : (M->x)) ;
        msize = M->type->size ;
        mnvec = M->nvec ;
        mvlen = M->vlen ;
        mnz = GB_NNZ (M) ;

        mthreads = GB_nthreads (mnz + M->nvec, chunk, nthreads_max) ;
        mtasks = (mthreads == 1) ? 1 : (8 * mthreads) ;
        if (!GB_ek_slice (&pstart_Mslice, &kfirst_Mslice, &klast_Mslice,
            M, &mtasks))
        {
            // out of memory
            return (GrB_OUT_OF_MEMORY) ;
        }

        // if M is sparse or hypersparse, scatter it into the C bitmap
        if (M_is_sparse_or_hyper)
        {
            #undef GB_MASK_WORK
            #define GB_MASK_WORK(pC) Cb [pC] += 2
            #include "GB_bitmap_assign_M_all_template.c"
            // the bitmap of C now contains:
            //  Cb (i,j) = 0:   cij not present, mij zero
            //  Cb (i,j) = 1:   cij present, mij zero
            //  Cb (i,j) = 2:   cij not present, mij 1
            //  Cb (i,j) = 3:   cij present, mij 1
        }
    }

    // C bitmap value for an entry to keep:
    // if M is sparse or hypersparse, and Mask_comp is false: 3
    // otherwise: 1
    const int8_t keep = M_is_sparse_or_hyper ? ((Mask_comp) ? 1 : 3) : 1 ;

    //--------------------------------------------------------------------------
    // select the method
    //--------------------------------------------------------------------------

    if (B_is_sparse_or_hyper)
    {

        //-----------------------------------------------------
        // C                =               A     *     B
        //-----------------------------------------------------

        // bitmap           .               bitmap      hyper
        // bitmap           .               full        hyper
        // bitmap           .               bitmap      sparse
        // bitmap           .               full        sparse

        //-----------------------------------------------------
        // C               <M>=             A     *     B
        //-----------------------------------------------------

        // bitmap           any             bitmap      hyper
        // bitmap           any             full        hyper
        // bitmap           any             bitmap      sparse
        // bitmap           any             full        sparse

        //-----------------------------------------------------
        // C               <!M>=            A     *     B
        //-----------------------------------------------------

        // bitmap           any             bitmap      hyper
        // bitmap           any             full        hyper
        // bitmap           any             bitmap      sparse
        // bitmap           any             full        sparse

        ASSERT (GB_IS_BITMAP (A) || GB_IS_FULL (A)) ;
        double work = ((double) avlen) * ((double) bnz) ;
        nthreads = GB_nthreads (work, chunk, nthreads_max) ;
        int naslice, nbslice ;

        if (nthreads == 1)
        {
            // do the entire computation with a single thread
            naslice = 1 ;
            nbslice = 1 ;
        }
        else
        {
            // determine number of slices for A and B
            ntasks = 8 * nthreads ;
            int dtasks = ceil (sqrt ((double) ntasks)) ;
            if (bnvec > dtasks || bnvec == 0)
            {
                // slice B into nbslice slices
                nbslice = dtasks ;
            }
            else
            {
                // slice B into one task per vector
                nbslice = bnvec ;
            }
            // slice A to get ntasks tasks
            naslice = ntasks / nbslice ;
            // but do not slice A too finely
            naslice = GB_IMIN (naslice, avlen) ;
            naslice = GB_IMAX (naslice, 1) ;
        }

        ntasks = naslice * nbslice ;

        // slice the matrix B
        if (!GB_pslice (&B_slice, Bp, bnvec, nbslice))
        { 
            // out of memory
            GB_ek_slice_free (&pstart_Mslice, &kfirst_Mslice, &klast_Mslice) ;
            return (GrB_OUT_OF_MEMORY) ;
        }

        if (M == NULL)
        {

            //------------------------------------------------------------------
            // C = A*B, no mask, A bitmap, B sparse
            //------------------------------------------------------------------

            // printf ("\n[bitmap C=A*B: A bitmap, B sparse]\n") ;
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_sparse_template.c"

        }
        else if (M_is_sparse_or_hyper)
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M sparse, A bitmap, B sparse
            //------------------------------------------------------------------

            // A is bitmap or full.  B is sparse or hypersparse.  scatter M or
            // !M into the C bitmap.  A sliced by rows and B by columns.  No
            // atomics.
            // printf ("\n[bitmap C<M sparse>=A*B: A bitmap, B sparse]\n") ;
            #define GB_MASK_IS_SPARSE
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_sparse_template.c"

        }
        else
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M bitmap, A bitmap, B sparse
            //------------------------------------------------------------------

            // Same as above, except that M can be used in-place, instead of
            // being copied into the C bitmap.
            // printf ("\n[bitmap C<M bitmap>=A*B: A bitmap, B sparse]\n") ;
            #define GB_MASK_IS_BITMAP
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_sparse_template.c"
        }

    }
    else if (A_is_sparse_or_hyper)
    {

        //-----------------------------------------------------
        // C                =               A     *     B
        //-----------------------------------------------------

        // bitmap           .               hyper       bitmap
        // bitmap           .               sparse      bitmap
        // bitmap           .               hyper       full 
        // bitmap           .               sparse      full

        //-----------------------------------------------------
        // C               <M>=             A     *     B
        //-----------------------------------------------------

        // bitmap           any             hyper       bitmap
        // bitmap           any             sparse      bitmap
        // bitmap           bitmap/full     hyper       full
        // bitmap           bitmap/full     sparse      full

        //-----------------------------------------------------
        // C               <!M>=            A     *     B
        //-----------------------------------------------------

        // bitmap           any             hyper       bitmap
        // bitmap           any             sparse      bitmap
        // bitmap           any             hyper       full 
        // bitmap           any             sparse      full

        ASSERT (GB_IS_BITMAP (B) || GB_IS_FULL (B)) ;
        double work = ((double) anz) * (double) bvdim ;
        nthreads = GB_nthreads (work, chunk, nthreads_max) ;
        int nfine_tasks_per_vector = 0 ;
        bool use_coarse_tasks ;

        // printf ("nthreads %d\n", nthreads) ;
        // printf ("bvdim %ld\n", bvdim) ;
        // printf ("anz %ld\n", anz) ;

        if (nthreads == 1 || bvdim == 0)
        {
            // do the entire computation with a single thread, with coarse task
            ntasks = 1 ;
            use_coarse_tasks = true ;
        }
        else
        {
            // determine number of tasks and select coarse/fine strategies
            ASSERT (bvdim > 0) ;
            ntasks = 8 * nthreads ;
            if (ntasks > bvdim)
            {
                // There are more tasks than vectors in B, so multiple fine
                // task are created for each vector of B.  All tasks are fine.
                // Determine how many fine tasks to create for each vector of
                // B.  Each group of fine tasks works on a single vector.
                use_coarse_tasks = false ;
                nfine_tasks_per_vector =
                    ceil ((double) ntasks / (double) bvdim) ;
                ntasks = bvdim * nfine_tasks_per_vector ;
                ASSERT (nfine_tasks_per_vector > 1) ;

                // slice the matrix A for each team of fine tasks
                if (!GB_pslice (&A_slice, Ap, anvec, nfine_tasks_per_vector))
                { 
                    // out of memory
                    GB_ek_slice_free (&pstart_Mslice, &kfirst_Mslice,
                        &klast_Mslice) ;
                    return (GrB_OUT_OF_MEMORY) ;
                }
            }
            else
            {
                // All tasks are coarse, and each coarse task does 1 or more
                // whole vectors of B
                use_coarse_tasks = true ;
            }
        }

        if (M == NULL)
        {

            //------------------------------------------------------------------
            // C = A*B, no mask, A sparse, B bitmap
            //------------------------------------------------------------------

            // Like GB_AxB_saxpy_C_sparse, except that all tasks are
            // coarse/fine Gustavson (fine Gustavson with atomics).  No
            // symbolic pre-analysis, and no Gustavson workspace.  C can be
            // modified in-place.
            // printf ("\n[bitmap C=A*B: A sparse, B bitmap]\n") ;
            #include "GB_bitmap_AxB_saxpy_A_sparse_B_bitmap_template.c"

        }
        else if (M_is_sparse_or_hyper)
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M sparse, A sparse, B bitmap
            //------------------------------------------------------------------

            // As above, except scatter M into the C bitmap.
            // printf ("\n[bitmap C<M sparse>=A*B: A sparse, B bitmap]\n") ;
            #define GB_MASK_IS_SPARSE
            #include "GB_bitmap_AxB_saxpy_A_sparse_B_bitmap_template.c"

        }
        else
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M bitmap, A sparse, B bitmap
            //------------------------------------------------------------------

            // As above, except use M in-place; do not scatter into the C
            // bitmap.
            // printf ("\n[bitmap C<M bitmap>=A*B: A sparse, B bitmap]\n") ;
            #define GB_MASK_IS_BITMAP
            #include "GB_bitmap_AxB_saxpy_A_sparse_B_bitmap_template.c"
        }

    }
    else
    {

        //-----------------------------------------------------
        // C                =               A     *     B
        //-----------------------------------------------------

        // bitmap           .               bitmap      bitmap
        // bitmap           .               full        bitmap
        // bitmap           .               bitmap      full
        // full             .               full        full

        //-----------------------------------------------------
        // C               <M>=             A     *     B
        //-----------------------------------------------------

        // bitmap           any             bitmap      bitmap
        // bitmap           any             full        bitmap
        // bitmap           bitmap/full     bitmap      full
        // bitmap           bitmap/full     full        full

        //-----------------------------------------------------
        // C               <!M>=            A     *     B
        //-----------------------------------------------------

        // bitmap           any             bitmap      bitmap
        // bitmap           any             full        bitmap
        // bitmap           any             bitmap      full
        // bitmap           any             full        full

        #define GB_TILE_SIZE 64
        #define GB_KTILE_SIZE 8

        double work = ((double) avlen) * ((double) bvlen) * ((double) bvdim) ;
        nthreads = GB_nthreads (work, chunk, nthreads_max) ;
        int64_t nI_tasks = (bvdim == 0) ? 1 : (1 + (bvdim-1) / GB_TILE_SIZE) ;
        int64_t nJ_tasks = (avlen == 0) ? 1 : (1 + (avlen-1) / GB_TILE_SIZE) ;
        int64_t ntasks = nI_tasks * nJ_tasks ;

        if (M == NULL)
        {

            //------------------------------------------------------------------
            // C = A*B, no mask, A bitmap, B bitmap
            //------------------------------------------------------------------

            // This method can used arbitrary tiling methods.  Divide up C
            // into K-by-K tiles for some chosen constant K, and compute
            // each C(i,j) tile independently.
            // printf ("\n[bitmap C=A*B: A bitmap, B bitmap]\n") ;
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_bitmap_template.c"

        }
        else if (M_is_sparse_or_hyper)
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M sparse, A bitmap, B bitmap
            //------------------------------------------------------------------

            // Same as above, except scatter M and !M into the C bitmap.
            // Before computing the C(i,j) tile, check the mask to see if any
            // entry is allowed to be modified by the mask, and skip the work.
            // If there are very few entries to compute in the C(i,j) tile,
            // could use a dot-product method instead, to compute each tile
            // multiply.
            // printf ("\n[bitmap C<M sparse>=A*B: A bitmap, B bitmap]\n") ;
            #define GB_MASK_IS_SPARSE
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_bitmap_template.c"

        }
        else
        {

            //------------------------------------------------------------------
            // C<M> or <!M> = A*B, M bitmap, A bitmap, B bitmap
            //------------------------------------------------------------------

            // As above, except use M or !M in-place.
            // printf ("\n[bitmap C<M bitmap>=A*B: A bitmap, B bitmap]\n") ;
            #define GB_MASK_IS_BITMAP
            #include "GB_bitmap_AxB_saxpy_A_bitmap_B_bitmap_template.c"
        }
    }

    C->nvals = cnvals ;

    //--------------------------------------------------------------------------
    // if M is sparse, clear it from the C bitmap
    //--------------------------------------------------------------------------

    if (M_is_sparse_or_hyper)
    {
        #undef GB_MASK_WORK
        #define GB_MASK_WORK(pC) Cb [pC] -= 2
        #include "GB_bitmap_assign_M_all_template.c"
    }

    //--------------------------------------------------------------------------
    // free workspace
    //--------------------------------------------------------------------------

    GB_FREE (A_slice) ;
    GB_FREE (B_slice) ;
    GB_ek_slice_free (&pstart_Mslice, &kfirst_Mslice, &klast_Mslice) ;
}
