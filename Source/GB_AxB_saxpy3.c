//------------------------------------------------------------------------------
// GB_AxB_saxpy3: compute C = A*B in parallel
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// This function computes C=A*B.  The mask is not applied.  A mix of
// Gustavson's method [1] and the Hash method [2] is used, per task.

// TODO add a method that exploits the mask.

// References:

// [1] Fred G. Gustavson. 1978. Two Fast Algorithms for Sparse
// Matrices: Multiplication and Permuted Transposition. ACM Trans. Math. Softw.
// 4, 3 (September 1978), 250–269. DOI:https://doi.org/10.1145/355791.355796

// [2] Yusuke Nagasaka, Satoshi Matsuoka, Ariful Azad, and Aydın Buluç. 2018.
// High-Performance Sparse Matrix-Matrix Products on Intel KNL and Multicore
// Architectures. In Proc. 47th Intl. Conf. on Parallel Processing (ICPP ’18).
// Association for Computing Machinery, New York, NY, USA, Article 34, 1–10.
// DOI:https://doi.org/10.1145/3229710.3229720

//------------------------------------------------------------------------------

#include "GB_mxm.h"
#include "GB_bracket.h"
#include "GB_sort.h"
#ifndef GBCOMPACT
#include "GB_AxB__include.h"
#endif

//------------------------------------------------------------------------------
// control parameters for generating parallel tasks
//------------------------------------------------------------------------------

#define GB_NTASKS_PER_THREAD 2
#define GB_COSTLY 1.2
#define GB_FINE_WORK 2

//------------------------------------------------------------------------------
// free workspace
//------------------------------------------------------------------------------

// This workspace is not needed in the GB_Asaxpy3B* worker functions.
#define GB_FREE_INITIAL_WORK                                                \
{                                                                           \
    GB_FREE_MEMORY (Bflops2, max_bjnz+1, sizeof (int64_t)) ;                \
    GB_FREE_MEMORY (Coarse_initial, ntasks_initial+1, sizeof (int64_t)) ;   \
    GB_FREE_MEMORY (Fine_slice, ntasks+1, sizeof (int64_t)) ;               \
}

// See also GB_FREE_ALL in Template/GB_AxB_saxpy3_template.c, which must
// match this definition, for the GB_Asaxpy3B* worker functions.
#define GB_FREE_WORK                                                        \
{                                                                           \
    GB_FREE_INITIAL_WORK ;                                                  \
    GB_FREE_MEMORY (*(TaskList_handle), ntasks, sizeof (GB_saxpy3task_struct));\
    GB_FREE_MEMORY (Hi_all, Hi_size_total, sizeof (int64_t)) ;              \
    GB_FREE_MEMORY (Hf_all, Hf_size_total, sizeof (int64_t)) ;              \
    GB_FREE_MEMORY (Hx_all, Hx_size_total, 1) ;                             \
}

#define GB_FREE_ALL             \
{                               \
    GB_FREE_WORK ;              \
    GB_MATRIX_FREE (Chandle) ;  \
}

//------------------------------------------------------------------------------
// GB_hash_table_size
//------------------------------------------------------------------------------

// flmax is the max flop count for computing A*B(:,j), for any vector j that
// this task computes.  GB_hash_table_size determines the hash table size for
// this task, which is twice the smallest power of 2 larger than flmax.  If
// flmax is large enough, the hash_size is returned as cvlen, so that
// Gustavson's method will be used instead of the Hash method.

static inline int64_t GB_hash_table_size
(
    int64_t flmax,      // max flop count for any vector computed by this task
    int64_t cvlen       // vector length of C
)
{
    // hash_size = 2 * (smallest power of 2 that is >= to flmax)
    double hlog = log2 ((double) flmax) ;
    int64_t hash_size = ((int64_t) 2) << ((int64_t) floor (hlog) + 1) ;
    // use Gustavson's method if hash_size is too big
    // TODO n/16 might be too big.  Use a parameter, and allow it to be
    // controlled by the user.  Select Gustavson if hash_size >= cvlen*alpha.
    // Limit alpha to between 0 and 1.
    // alpha=0 means always use Gustavson.
    // alpha=1 means always use Hash, unless the hash size is >= cvlen.
    bool use_Gustavson = (hash_size >= cvlen/16) ;  // TODO use alpha param
    if (use_Gustavson)
    {
        hash_size = cvlen ;
    }
    return (hash_size) ;
}

//------------------------------------------------------------------------------
// GB_create_coarse_task: create a single coarse task
//------------------------------------------------------------------------------

// Compute the max flop count for any vector in a coarse task, determine the
// hash table size, and construct the coarse task.

static inline void GB_create_coarse_task
(
    int64_t kfirst,     // coarse task consists of vectors kfirst:klast
    int64_t klast,
    GB_saxpy3task_struct *TaskList,
    int taskid,         // taskid for this coarse task
    int64_t *Bflops,    // size bnvec; cum sum of flop counts for vectors of B
    int64_t cvlen,      // vector length of B and C
    double chunk,
    int nthreads_max
)
{
    // find the max # of flops for any vector in this task
    int64_t flmax = 1 ;
    int nth = GB_nthreads (klast-kfirst+1, chunk, nthreads_max) ;
    int64_t kk ;
    #pragma omp parallel for num_threads(nth) schedule(static) \
        reduction(max:flmax)
    for (kk = kfirst ; kk <= klast ; kk++)
    {
        int64_t fl = Bflops [kk+1] - Bflops [kk] ;
        flmax = GB_IMAX (flmax, fl) ;
    }
    // define the coarse task
    TaskList [taskid].start  = kfirst ;
    TaskList [taskid].end    = klast ;
    TaskList [taskid].vector = -1 ;
    TaskList [taskid].master = taskid ;
    TaskList [taskid].hsize  = GB_hash_table_size (flmax, cvlen) ;
    TaskList [taskid].flops  = Bflops [klast+1] - Bflops [kfirst] ;

    // printf ("    create final coarse task "GBd":"GBd" flops "GBd"\n",
    //     kfirst, klast, TaskList [taskid].flops) ;
}

//------------------------------------------------------------------------------
// GB_AxB_saxpy3: compute C = A*B in parallel
//------------------------------------------------------------------------------

GrB_Info GB_AxB_saxpy3              // C = A*B using Gustavson+Hash
(
    GrB_Matrix *Chandle,            // output matrix
    const GrB_Matrix A,             // input matrix
    const GrB_Matrix B,             // input matrix
    const GrB_Semiring semiring,    // semiring that defines C=A*B
    const bool flipxy,              // if true, do z=fmult(b,a) vs fmult(a,b)
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    // printf ("saxpy3 start, flipxy %d\n", flipxy) ;

    GrB_Info info ;
    ASSERT (Chandle != NULL) ;
    ASSERT (*Chandle == NULL) ;
    ASSERT_MATRIX_OK (A, "A for saxpy3 A*B", GB0) ;
    ASSERT_MATRIX_OK (B, "B for saxpy3 A*B", GB0) ;
    ASSERT (!GB_PENDING (A)) ; ASSERT (!GB_ZOMBIES (A)) ;
    ASSERT (!GB_PENDING (B)) ; ASSERT (!GB_ZOMBIES (B)) ;
    ASSERT_SEMIRING_OK (semiring, "semiring for saxpy3 A*B", GB0) ;
    ASSERT (A->vdim == B->vlen) ;

    int64_t *GB_RESTRICT Hi_all = NULL ;
    int64_t *GB_RESTRICT Hf_all = NULL ;
    GB_void *GB_RESTRICT Hx_all = NULL ;
    int64_t *GB_RESTRICT Coarse_initial = NULL ;    // initial coarse tasks
    GB_saxpy3task_struct *GB_RESTRICT TaskList = NULL ;
    GB_saxpy3task_struct *GB_RESTRICT *TaskList_handle = &(TaskList) ;
    int64_t *GB_RESTRICT Fine_slice = NULL ;
    int64_t *GB_RESTRICT Bflops2 = NULL ;

    int ntasks = 0 ;
    int ntasks_initial = 0 ;
    size_t Hi_size_total = 0 ;
    size_t Hf_size_total = 0 ;
    size_t Hx_size_total = 0 ;
    int64_t max_bjnz = 0 ;

    //--------------------------------------------------------------------------
    // get the semiring operators
    //--------------------------------------------------------------------------

    GrB_BinaryOp mult = semiring->multiply ;
    GrB_Monoid add = semiring->add ;
    ASSERT (mult->ztype == add->op->ztype) ;

    bool op_is_first  = mult->opcode == GB_FIRST_opcode ;
    bool op_is_second = mult->opcode == GB_SECOND_opcode ;
    bool A_is_pattern = false ;
    bool B_is_pattern = false ;

    if (flipxy)
    { 
        // z = fmult (b,a) will be computed
        A_is_pattern = op_is_first  ;
        B_is_pattern = op_is_second ;
        ASSERT (GB_IMPLIES (!A_is_pattern,
            GB_Type_compatible (A->type, mult->ytype))) ;
        ASSERT (GB_IMPLIES (!B_is_pattern,
            GB_Type_compatible (B->type, mult->xtype))) ;
    }
    else
    { 
        // z = fmult (a,b) will be computed
        A_is_pattern = op_is_second ;
        B_is_pattern = op_is_first  ;
        ASSERT (GB_IMPLIES (!A_is_pattern,
            GB_Type_compatible (A->type, mult->xtype))) ;
        ASSERT (GB_IMPLIES (!B_is_pattern,
            GB_Type_compatible (B->type, mult->ytype))) ;
    }

    (*Chandle) = NULL ;

    //--------------------------------------------------------------------------
    // get A, and B
    //--------------------------------------------------------------------------

    const int64_t *GB_RESTRICT Ap = A->p ;
    const int64_t *GB_RESTRICT Ah = A->h ;
    // const int64_t *GB_RESTRICT Ai = A->i ;
    const int64_t avlen = A->vlen ;
    const int64_t avdim = A->vdim ;
    // const int64_t anz = GB_NNZ (A) ;
    const int64_t anvec = A->nvec ;
    const bool A_is_hyper = A->is_hyper ;

    const int64_t *GB_RESTRICT Bp = B->p ;
    const int64_t *GB_RESTRICT Bh = B->h ;
    const int64_t *GB_RESTRICT Bi = B->i ;
    const int64_t bvlen = B->vlen ;
    const int64_t bvdim = B->vdim ;
    // const int64_t bnz = GB_NNZ (B) ;
    const int64_t bnvec = B->nvec ;
    const bool B_is_hyper = B->is_hyper ;

    //--------------------------------------------------------------------------
    // determine the # of threads to use
    //--------------------------------------------------------------------------

    GB_GET_NTHREADS_MAX (nthreads_max, chunk, Context) ;

    // nthreads_max = 1 ;

    //--------------------------------------------------------------------------
    // allocate C (just C->p and C->h, but not C->i or C->x)
    //--------------------------------------------------------------------------

    GrB_Type ctype = add->op->ztype ;
    size_t csize = ctype->size ;
    int64_t cvlen = avlen ;
    int64_t cvdim = bvdim ;
    int64_t cnz = 0 ;
    int64_t cnvec = bnvec ;
    bool C_is_hyper = (cvdim > 1) && (A_is_hyper || B_is_hyper) ;

    GB_NEW (Chandle, ctype, cvlen, cvdim, GB_Ap_calloc, true,
        GB_SAME_HYPER_AS (B_is_hyper), B->hyper_ratio, cnvec, Context) ;
    if (info != GrB_SUCCESS)
    { 
        // out of memory
        GB_FREE_ALL ;
        return (info) ;
    }

    // TODO do not allocate C->h until the very end, in GB_hypermatrix_prune

    GrB_Matrix C = (*Chandle) ;

    int64_t *GB_RESTRICT Cp = C->p ;
    int64_t *GB_RESTRICT Ch = C->h ;
    if (B_is_hyper)
    { 
        // C has the same set of vectors as B
        int nth = GB_nthreads (cnvec, chunk, nthreads_max) ;
        GB_memcpy (Ch, Bh, cnvec * sizeof (int64_t), nth) ;
        C->nvec = bnvec ;
    }

    //==========================================================================
    // phase0: create parallel tasks
    //==========================================================================

    //--------------------------------------------------------------------------
    // compute flop counts for each vector of B and C
    //--------------------------------------------------------------------------

    int64_t *GB_RESTRICT Bflops = Cp ;  // Cp is used as workspace for Bflops
    bool ignore ;
    GB_OK (GB_AxB_flopcount (&ignore, Bflops, NULL, NULL, A, B, 0, Context)) ;
    int64_t total_flops = Bflops [bnvec] ;

    // printf ("total_flops "GBd"\n", total_flops) ;
    // printf ("cnvec: "GBd" cplen "GBd"\n", cnvec, C->plen) ;
    // for (int k = 0 ; k <= bnvec ; k++)
    // {
    //     printf ("Bflops [%d] = "GBd"\n", k, Bflops [k]) ;
    // }

    //--------------------------------------------------------------------------
    // determine # of threads and # of initial coarse tasks
    //--------------------------------------------------------------------------

    int nthreads = GB_nthreads ((double) total_flops, chunk, nthreads_max) ;
    ntasks_initial = (nthreads == 1) ?  1 : (GB_NTASKS_PER_THREAD * nthreads) ;
    double target_task_size = ((double) total_flops) / ntasks_initial ;
    target_task_size = GB_IMAX (target_task_size, chunk) ;
    double target_fine_size = target_task_size / GB_FINE_WORK ;
    target_fine_size = GB_IMAX (target_fine_size, chunk) ;

    // printf ("nthreads %d\n", nthreads) ;
    // printf ("chunk %g\n", chunk) ;
    // printf ("target_task_size %g\n", target_task_size) ;
    // printf ("target_fine_size %g\n", target_fine_size) ;

    //--------------------------------------------------------------------------
    // determine # of parallel tasks
    //--------------------------------------------------------------------------

    int nfine = 0 ;         // # of fine tasks
    int ncoarse = 0 ;       // # of coarse tasks
    max_bjnz = 0 ;          // max (nnz (B (:,j))) of fine tasks

    // FUTURE: also use ultra-fine tasks that compute A(i1:i2,k)*B(k,j)

    if (ntasks_initial > 1)
    {

        //----------------------------------------------------------------------
        // construct initial coarse tasks
        //----------------------------------------------------------------------

        if (!GB_pslice (&Coarse_initial, Bflops, bnvec, ntasks_initial))
        {
            // out of memory
            GB_FREE_ALL ;
            return (GB_OUT_OF_MEMORY) ;
        }

        //----------------------------------------------------------------------
        // split the work into coarse and fine tasks
        //----------------------------------------------------------------------

        for (int taskid = 0 ; taskid < ntasks_initial ; taskid++)
        {
            // get the initial coarse task
            int64_t kfirst = Coarse_initial [taskid] ;
            int64_t klast  = Coarse_initial [taskid+1] ;
            int64_t task_ncols = klast - kfirst ;
            int64_t task_flops = Bflops [klast] - Bflops [kfirst] ;

            // printf ("init task %d\n", taskid) ;
            // printf ("    kfirst "GBd" klast "GBd" ncols "GBd" flops "GBd"\n",
            //     kfirst, klast, task_ncols, task_flops) ;

            if (task_ncols == 0)
            {
                // This coarse task is empty, having been squeezed out by
                // costly vectors in adjacent coarse tasks.
                // printf ("   init coarse task is empty\n") ;
            }
            else if (task_flops > 2 * GB_COSTLY * target_task_size)
            {
                // This coarse task is too costly, because it contains one or
                // more costly vectors.  Split its vectors into a mixture of
                // coarse and fine tasks.
                // printf ("   init coarse task is costly\n") ;

                int64_t kcoarse_start = kfirst ;

                for (int64_t kk = kfirst ; kk < klast ; kk++)
                {
                    // jflops = # of flops to compute a single vector A*B(:,j)
                    // where j == (Bh == NULL) ? kk : Bh [kk].
                    double jflops = Bflops [kk+1] - Bflops [kk] ;
                    // bjnz = nnz (B (:,j))
                    int64_t bjnz = Bp [kk+1] - Bp [kk] ;

                    // printf ("   jflops %g\n" , jflops) ;
                    // printf ("   bjnz "GBd"\n" , bjnz) ;

                    if (jflops > GB_COSTLY * target_task_size && bjnz > 1)
                    {
                        // A*B(:,j) is costly; split it into 2 or more fine
                        // tasks.  First flush the prior coarse task, if any.
                        if (kcoarse_start < kk)
                        {
                            // vectors kcoarse_start to kk-1 form a single
                            // coarse task
                            // printf ("   flush prior coarse\n") ;
                            ncoarse++ ;
                        }

                        // next coarse task (if any) starts at kk+1
                        kcoarse_start = kk+1 ;

                        // vectors kk will be split into multiple fine tasks
                        max_bjnz = GB_IMAX (max_bjnz, bjnz) ;
                        int nfine_team_size = ceil (jflops / target_fine_size) ;
                        nfine += nfine_team_size ;
                        // printf ("   team size %d\n", nfine_team_size) ;
                    }
                }

                // flush the last coarse task, if any
                if (kcoarse_start < klast)
                {
                    // vectors kcoarse_start to klast-1 form a single
                    // coarse task
                    ncoarse++ ;
                }

            }
            else
            {
                // This coarse task is OK as-is.
                // printf ("   init coarse task is OK\n") ;
                ncoarse++ ;
            }
        }
    }
    else
    {

        //----------------------------------------------------------------------
        // entire computation in a single coarse task
        //----------------------------------------------------------------------

        // printf ("   just one task\n") ;
        nfine = 0 ;
        ncoarse = 1 ;
    }

    ntasks = ncoarse + nfine ;

    //--------------------------------------------------------------------------
    // allocate the tasks, and workspace to construct fine tasks
    //--------------------------------------------------------------------------

    GB_CALLOC_MEMORY (TaskList, ntasks, sizeof (GB_saxpy3task_struct)) ;
    GB_MALLOC_MEMORY (Fine_slice, ntasks+1, sizeof (int64_t)) ;
    GB_MALLOC_MEMORY (Bflops2, max_bjnz+1, sizeof (int64_t)) ;

    if (TaskList == NULL || Fine_slice == NULL || Bflops2 == NULL)
    {
        // out of memory
        GB_FREE_ALL ;
        return (GB_OUT_OF_MEMORY) ;
    }

    //--------------------------------------------------------------------------
    // create the tasks
    //--------------------------------------------------------------------------

    // printf ("\n======= create the tasks (fine: %d, coarse: %d)\n",
    //     nfine, ncoarse) ;

    if (ntasks_initial > 1)
    {

        //----------------------------------------------------------------------
        // create the coarse and fine tasks
        //----------------------------------------------------------------------

        int nf = 0 ;        // fine tasks have task id 0:nfine-1
        int nc = nfine ;    // coarse task ids are nfine:ntasks-1

        for (int taskid = 0 ; taskid < ntasks_initial ; taskid++)
        {
            // get the initial coarse task
            int64_t kfirst = Coarse_initial [taskid] ;
            int64_t klast  = Coarse_initial [taskid+1] ;
            int64_t task_ncols = klast - kfirst ;
            int64_t task_flops = Bflops [klast] - Bflops [kfirst] ;

            // printf ("init task %d\n", taskid) ;
            // printf ("    kfirst "GBd" klast "GBd" ncols "GBd" flops "GBd"\n",
            //     kfirst, klast, task_ncols, task_flops) ;

            if (task_ncols == 0)
            {
                // This coarse task is empty, having been squeezed out by
                // costly vectors in adjacent coarse tasks.
                // printf ("   init coarse task is empty\n") ;
            }
            else if (task_flops > 2 * GB_COSTLY * target_task_size)
            {
                // This coarse task is too costly, because it contains one or
                // more costly vectors.  Split its vectors into a mixture of
                // coarse and fine tasks.
                // printf ("   init coarse task is costly\n") ;

                int64_t kcoarse_start = kfirst ;

                for (int64_t kk = kfirst ; kk < klast ; kk++)
                {
                    // jflops = # of flops to compute a single vector A*B(:,j)
                    // where j == (Bh == NULL) ? kk : Bh [kk].
                    double jflops = Bflops [kk+1] - Bflops [kk] ;
                    // bjnz = nnz (B (:,j))
                    int64_t bjnz = Bp [kk+1] - Bp [kk] ;

                    // printf ("   jflops %g\n" , jflops) ;
                    // printf ("   bjnz "GBd"\n" , bjnz) ;

                    if (jflops > GB_COSTLY * target_task_size && bjnz > 1)
                    {
                        // A*B(:,j) is costly; split it into 2 or more fine
                        // tasks.  First flush the prior coarse task, if any.
                        if (kcoarse_start < kk)
                        {
                            // kcoarse_start:kk-1 form a single coarse task
                            GB_create_coarse_task (kcoarse_start, kk-1,
                                TaskList, nc++, Bflops, cvlen,
                                chunk, nthreads_max) ;
                        }

                        // next coarse task (if any) starts at kk+1
                        kcoarse_start = kk+1 ;

                        // count the work for each entry B(k,j)
                        int64_t pB_start = Bp [kk] ;
                        int nth = GB_nthreads (bjnz, chunk, nthreads_max) ;
                        int64_t s ;
                        #pragma omp parallel for num_threads(nth) \
                            schedule(static)
                        for (s = 0 ; s < bjnz ; s++)
                        {
                            // get B(k,j)
                            int64_t k = Bi [pB_start + s] ;
                            // fl = flop count for just A(:,k)*B(k,j)
                            int64_t pA_start, pA_end ;
                            int64_t pleft = 0 ;
                            GB_lookup (A_is_hyper, Ah, Ap, &pleft, anvec-1, k,
                                &pA_start, &pA_end) ;
                            int64_t fl = pA_end - pA_start ;
                            Bflops2 [s] = fl ;
                            ASSERT (fl >= 0) ;
                        }

                        // for (s = 0 ; s < bjnz ; s++)
                        // {
                            // printf ("Bflops2 ["GBd"] = "GBd"\n", s, 
                            // Bflops2 [s]) ;
                        // }

                        // cumulative sum of flops to compute A*B(:,j)
                        GB_cumsum (Bflops2, bjnz, NULL, nth) ;

                        // for (s = 0 ; s <= bjnz ; s++)
                        // {
                            // printf ("Bflops2 ["GBd"] = "GBd"\n", s, 
                            // Bflops2 [s]) ;
                        // }

                        // printf ("\ncumsum:\n") ;
                        // for (s = 0 ; s < bjnz ; s++)
                        // {
                        //     printf ("Bflops2 ["GBd"] = "GBd"\n", s, 
                        //         Bflops2 [s]) ;
                        // }

                        // slice B(:,j) into fine tasks
                        int nfine_team_size = ceil (jflops / target_fine_size) ;
                        ASSERT (Fine_slice != NULL) ;
                        GB_pslice (&Fine_slice, Bflops2, bjnz, nfine_team_size);

                        // shared hash table for all fine tasks for A*B(:,j)
                        int64_t hsize = GB_hash_table_size (jflops, cvlen) ;

                        // construct the fine tasks for C(:,j)=A*B(:,j)
                        int master = nf ;
                        for (int fid = 0 ; fid < nfine_team_size ; fid++)
                        {
                            int64_t pstart = Fine_slice [fid] ;
                            int64_t pend   = Fine_slice [fid+1] ;
                            int64_t fl = Bflops2 [pend] - Bflops2 [pstart] ;
                            TaskList [nf].start  = pB_start + pstart ;
                            TaskList [nf].end    = pB_start + pend - 1 ;
                            TaskList [nf].vector = kk ;
                            TaskList [nf].hsize  = hsize ;
                            TaskList [nf].master = master ;
                            TaskList [nf].nfine_team_size = nfine_team_size ;
                            TaskList [nf].flops = fl ;
//    printf ("    create final fine task "GBd":"GBd" flops "GBd"\n",
//        pstart, pend, fl) ;
                            nf++ ;
                        }
                    }
                }

                // flush the last coarse task, if any
                if (kcoarse_start < klast)
                {
                    // kcoarse_start:klast-1 form a single coarse task
                    GB_create_coarse_task (kcoarse_start, klast-1,
                        TaskList, nc++, Bflops, cvlen, chunk, nthreads_max) ;
                }

            }
            else
            {
                // This coarse task is OK as-is.
                GB_create_coarse_task (kfirst, klast-1,
                    TaskList, nc++, Bflops, cvlen, chunk, nthreads_max) ;
            }
        }

    }
    else
    {

        //----------------------------------------------------------------------
        // entire computation in a single coarse task
        //----------------------------------------------------------------------

        GB_create_coarse_task (0, bnvec-1,
            TaskList, 0, Bflops, cvlen, chunk, nthreads_max) ;
    }

    //--------------------------------------------------------------------------
    // free workspace used to create the tasks
    //--------------------------------------------------------------------------

    // Frees Bflops2, Coarse_initial, and Fine_slice.  These do not need to
    // be freed in the GB_Asaxpy3B worker below.

    GB_FREE_INITIAL_WORK ;

    //--------------------------------------------------------------------------

#if 0
    // dump the task descriptors
    printf ("\n================== final tasks: ncoarse %d nfine %d ntasks %d\n",
        ncoarse, nfine, ntasks) ;

    for (int fid = 0 ; fid < nfine ; fid++)
    {
        // computes C(:,j) += A*B(k1:k2,j)
        // note that j = (Bh == NULL) ? kk : Bh [kk]
        int64_t kk = TaskList [fid].vector ;
        ASSERT (kk >= 0) ;
        int64_t pB_start = Bp [kk] ;
        int64_t p1 = TaskList [fid].start - pB_start ;
        int64_t p2 = TaskList [fid].end   - pB_start ;
        int64_t hsize = TaskList [fid].hsize   ;
        int master = TaskList [fid].master ;
        double work = TaskList [fid].flops ;
        printf ("Fine %3d: ["GBd"] ("GBd" : "GBd") hsize/n %g master %d ",
            fid, kk, p1, p2, ((double) hsize) / ((double) cvlen),
            master) ;
        printf (" work %g work/n %g\n", work, work/cvlen) ;
    }

    for (int cid = nfine ; cid < ntasks ; cid++)
    {
        int64_t kfirst = TaskList [cid].start ;
        int64_t klast = TaskList [cid].end ;
        int64_t hsize = TaskList [cid].hsize ;
        double work = TaskList [cid].flops ;
        printf ("Coarse %3d: ["GBd" : "GBd"] work/tot: %g hsize/n %g ",
            cid, kfirst, klast, work / total_flops,
            ((double) hsize) / ((double) cvlen)) ;
        if (cid != TaskList [cid].master) printf ("hey master!\n") ;
        printf (" work %g work/n %g\n", work, work/cvlen) ;
        int64_t kk = TaskList [cid].vector ;
        ASSERT (kk < 0) ;
    }

#endif

    #if 0
    int nfine_hash = 0 ;
    int nfine_gus = 0 ;
    int ncoarse_hash = 0 ;
    int ncoarse_1hash = 0 ;
    int ncoarse_gus = 0 ;
    for (int taskid = 0 ; taskid < ntasks ; taskid++)
    {
        int64_t hash_size = TaskList [taskid].hsize ;
        int64_t kk = TaskList [taskid].vector ;
        bool is_fine = (kk >= 0) ;
        bool use_Gustavson = (hash_size == cvlen) ;
        if (is_fine)
        {
            // fine task
            if (use_Gustavson)
            {
                // fine Gustavson task
                nfine_gus++ ;
            }
            else
            {
                // fine hash task
                nfine_hash++ ;
            }
        }
        else
        {
            // coarse task
            int64_t kfirst = TaskList [taskid].start ;
            int64_t klast = TaskList [taskid].end ;
            int64_t nk = klast - kfirst + 1 ;
            if (use_Gustavson)
            {
                // coarse Gustavson task
                ncoarse_gus++ ;
            }
            else if (nk == 1)
            {
                // 1-vector coarse hash task
                ncoarse_1hash++ ;
            }
            else
            {
                // multi-vector coarse hash task
                ncoarse_hash++ ;
            }
        }
    }

    printf ("nthreads %d ntasks %d coarse: (gus: %d 1hash: %d hash: %d)"
        " fine: (gus: %d hash: %d)\n", nthreads, ntasks,
        ncoarse_gus, ncoarse_1hash, ncoarse_hash,
        nfine_gus, nfine_hash) ;
    #endif

    // Bflops is no longer needed as an alias for Cp
    Bflops = NULL ;

    //--------------------------------------------------------------------------
    // allocate the hash tables
    //--------------------------------------------------------------------------

    // If Gustavson's method is used (multi-vector coarse tasks):
    //
    //      hash_size is cvlen.
    //      Hi is not allocated.
    //      Hf and Hx are both of size hash_size.
    //
    //      (Hf [i] == mark) is true if i is in the hash table.
    //      Hx [i] is the value of C(i,j) during the numeric phase.
    //
    //      Gustavson's method is used if the hash_size for the Hash method
    //      is a significant fraction of cvlen. 
    //
    // If the Hash method is used (coarse tasks):
    //
    //      hash_size is 2 times the smallest power of 2 that is larger than
    //      the # of flops required for any column C(:,j) being computed.  This
    //      ensures that all entries have space in the hash table, and that the
    //      hash occupancy will never be more than 50%.  It is always smaller
    //      than cvlen (otherwise, Gustavson's method is used).
    //
    //      A hash function is used for the ith entry:
    //          hash = (i * GB_HASH_FACTOR) & (hash_size-1)
    //      If a collision occurs, linear probing is used:
    //          hash = (hash + 1) & (hashsize-1)
    //
    //      (Hf [hash] == mark) is true if the position is occupied.
    //      i = Hi [hash] gives the row index i that occupies that position.
    //      Hx [hash] is the value of C(i,j) during the numeric phase.
    //
    // For both coarse methods:
    //
    //      Hf starts out all zero (via calloc), and mark starts out as 1.  To
    //      clear all of Hf, mark is incremented, so that all entries in Hf are
    //      not equal to mark.

    // add some padding to the end of each hash table, to avoid false
    // sharing of cache lines between the hash tables.
    size_t hx_pad = 64 ;
    size_t hi_pad = 64 / sizeof (int64_t) ;

    Hi_size_total = 0 ;
    Hf_size_total = 0 ;
    Hx_size_total = 0 ;

    // determine the total size of all hash tables
    for (int taskid = 0 ; taskid < ntasks ; taskid++)
    {
        if (taskid != TaskList [taskid].master)
        {
            // allocate a single shared hash table for all fine
            // tasks that compute a single C(:,j)
            continue ;
        }

        int64_t hash_size = TaskList [taskid].hsize ;
        int64_t k = TaskList [taskid].vector ;
        bool is_fine = (k >= 0) ;
        bool use_Gustavson = (hash_size == cvlen) ;
        int64_t kfirst = TaskList [taskid].start ;
        int64_t klast = TaskList [taskid].end ;
        int64_t nk = klast - kfirst + 1 ;

        if (is_fine && use_Gustavson)
        {
            // Hf is uint8_t for the fine Gustavson tasks, but round up
            // to the nearest number of int64_t values.
            Hf_size_total += GB_CEIL ((hash_size + hi_pad), sizeof (int64_t)) ;
        }
        else
        {
            // all other methods use Hf as int64_t
            Hf_size_total += (hash_size + hi_pad) ;
        }
        if (!is_fine && !use_Gustavson && nk > 1)
        {
            // only multi-vector coarse hash tasks need Hi
            Hi_size_total += (hash_size + hi_pad) ;
        }
        // all tasks use an Hx array of size hash_size
        Hx_size_total += (hash_size * csize + hx_pad) ;
    }

    #if 0
    printf ("Hi_size_total "GBd" int64\n", Hi_size_total) ;
    printf ("Hf_size_total "GBd" int64\n", Hf_size_total) ;
    printf ("Hx_size_total "GBd" bytes\n", Hx_size_total) ;
    printf ("csize: %g\n", (double) csize) ;
    #endif

    // allocate space for all hash tables
    GB_MALLOC_MEMORY (Hi_all, Hi_size_total, sizeof (int64_t)) ;
    GB_CALLOC_MEMORY (Hf_all, Hf_size_total, sizeof (int64_t)) ;
    GB_MALLOC_MEMORY (Hx_all, Hx_size_total, 1) ;

    if (Hi_all == NULL || Hf_all == NULL || Hx_all == NULL)
    {
        // out of memory
        GB_FREE_ALL ;
        return (GB_OUT_OF_MEMORY) ;
    }

    // split the space into separate hash tables
    int64_t *GB_RESTRICT Hi_split = Hi_all ;
    int64_t *GB_RESTRICT Hf_split = Hf_all ;
    GB_void *GB_RESTRICT Hx_split = Hx_all ;

    for (int taskid = 0 ; taskid < ntasks ; taskid++)
    {
        if (taskid != TaskList [taskid].master)
        {
            // allocate a single hash table for all fine
            // tasks that compute a single C(:,j)
            continue ;
        }

        TaskList [taskid].Hi = Hi_split ;
        TaskList [taskid].Hf = Hf_split ;
        TaskList [taskid].Hx = Hx_split ;

        int64_t hash_size = TaskList [taskid].hsize ;
        int64_t k = TaskList [taskid].vector ;
        bool is_fine = (k >= 0) ;
        bool use_Gustavson = (hash_size == cvlen) ;
        int64_t kfirst = TaskList [taskid].start ;
        int64_t klast = TaskList [taskid].end ;
        int64_t nk = klast - kfirst + 1 ;

        if (is_fine && use_Gustavson)
        {
            // Hf is uint8_t for the fine Gustavson method
            Hf_split += GB_CEIL ((hash_size + hi_pad), sizeof (int64_t)) ;
        }
        else
        {
            // Hf is int64_t for all other methods
            Hf_split += (hash_size + hi_pad) ;
        }
        if (!is_fine && !use_Gustavson && nk > 1)
        {
            // only multi-vector coarse hash tasks need Hi
            Hi_split += (hash_size + hi_pad) ;
        }
        // all tasks use an Hx array of size hash_size
        Hx_split += (hash_size * csize + hx_pad) ;
    }

    // assign shared hash tables to fine task teams
    for (int taskid = 0 ; taskid < nfine ; taskid++)
    {
        int master = TaskList [taskid].master ;
        ASSERT (TaskList [master].vector >= 0) ;
        if (taskid != master)
        {
            // this fine task (Gustavson or hash) shares its hash table
            // with all other tasks in its team, for a single vector C(:,j).
            ASSERT (TaskList [taskid].vector == TaskList [master].vector) ;
            TaskList [taskid].Hf = TaskList [master].Hf ;
            TaskList [taskid].Hx = TaskList [master].Hx ;
        }
    }

    //==========================================================================
    // C = A*B, via saxpy3 method and built-in semiring
    //==========================================================================

    bool done = false ;

    // pass the workspace to the worker, so it can be freed if any error occurs
    void *Work [3] ;
    size_t Worksize [3] ;
    Work [0] = Hi_all ;
    Work [1] = Hf_all ;
    Work [2] = Hx_all ;
    Worksize [0] = Hi_size_total ;
    Worksize [1] = Hf_size_total ;
    Worksize [2] = Hx_size_total ;

#ifndef GBCOMPACT

    //--------------------------------------------------------------------------
    // define the worker for the switch factory
    //--------------------------------------------------------------------------

    #define GB_Asaxpy3B(add,mult,xyname) GB_Asaxpy3B_ ## add ## mult ## xyname

    #define GB_AxB_WORKER(add,mult,xyname)                              \
    {                                                                   \
        info = GB_Asaxpy3B (add,mult,xyname) (Chandle,                  \
            A, A_is_pattern, B, B_is_pattern,                           \
            TaskList_handle, Work, Worksize, ntasks, nfine, nthreads,   \
            Context) ;                                                  \
        done = (info != GrB_NO_VALUE) ;                                 \
    }                                                                   \
    break ;

    //--------------------------------------------------------------------------
    // launch the switch factory
    //--------------------------------------------------------------------------

    GB_Opcode mult_opcode, add_opcode ;
    GB_Type_code xycode, zcode ;

    if (GB_AxB_semiring_builtin (A, A_is_pattern, B, B_is_pattern, semiring,
        flipxy, &mult_opcode, &add_opcode, &xycode, &zcode))
    { 
        #include "GB_AxB_factory.c"
    }

#endif

    //==========================================================================
    // C = A*B, via user semirings created at compile time
    //==========================================================================

    if (semiring->object_kind == GB_USER_COMPILED)
    { 
        // determine the required type of A and B for the user semiring
        GrB_Type atype_required, btype_required ;

        if (flipxy)
        { 
            // A is passed as y, and B as x, in z = mult(x,y)
            atype_required = mult->ytype ;
            btype_required = mult->xtype ;
        }
        else
        { 
            // A is passed as x, and B as y, in z = mult(x,y)
            atype_required = mult->xtype ;
            btype_required = mult->ytype ;
        }

        if (A->type == atype_required && B->type == btype_required)
        {
            // TODO add Context, and use TaskList_handle for saxpy3
            // add Work and Worksize parameters too
            info = GB_AxB_user (GxB_DEFAULT, semiring, Chandle, NULL, A, B,
                flipxy,
                /* heap: */ NULL, NULL, NULL, 0,
                /* Gustavson: */ NULL,
                /* dot2: */ NULL, NULL, nthreads, 0, 0, NULL,
                /* dot3 and saxpy3: */ TaskList, ntasks) ;
            done = true ;
        }
    }

    //==========================================================================
    // C = A*B, via the generic saxpy3 method, with typecasting
    //==========================================================================

    if (!done)
    {

        //----------------------------------------------------------------------
        // get operators, functions, workspace, contents of A, B, and C
        //----------------------------------------------------------------------

        GxB_binary_function fmult = mult->function ;
        GxB_binary_function fadd  = add->op->function ;

        size_t csize = C->type->size ;
        size_t asize = A_is_pattern ? 0 : A->type->size ;
        size_t bsize = B_is_pattern ? 0 : B->type->size ;

        size_t xsize = mult->xtype->size ;
        size_t ysize = mult->ytype->size ;

        // scalar workspace: because of typecasting, the x/y types need not
        // be the same as the size of the A and B types.
        // flipxy false: aik = (xtype) A(i,k) and bkj = (ytype) B(k,j)
        // flipxy true:  aik = (ytype) A(i,k) and bkj = (xtype) B(k,j)
        size_t aik_size = flipxy ? ysize : xsize ;
        size_t bkj_size = flipxy ? xsize : ysize ;

        GB_void *GB_RESTRICT terminal = add->terminal ;
        GB_void *GB_RESTRICT identity = add->identity ;

        GB_cast_function cast_A, cast_B ;
        if (flipxy)
        { 
            // A is typecasted to y, and B is typecasted to x
            cast_A = A_is_pattern ? NULL : 
                     GB_cast_factory (mult->ytype->code, A->type->code) ;
            cast_B = B_is_pattern ? NULL : 
                     GB_cast_factory (mult->xtype->code, B->type->code) ;
        }
        else
        { 
            // A is typecasted to x, and B is typecasted to y
            cast_A = A_is_pattern ? NULL :
                     GB_cast_factory (mult->xtype->code, A->type->code) ;
            cast_B = B_is_pattern ? NULL :
                     GB_cast_factory (mult->ytype->code, B->type->code) ;
        }

        //----------------------------------------------------------------------
        // C = A*B via saxpy3 method, function pointers, and typecasting
        //----------------------------------------------------------------------

        #define GB_IDENTITY identity

        // aik = A(i,k), located in Ax [pA]
        #define GB_GETA(aik,Ax,pA)                                          \
            GB_void aik [GB_VLA(aik_size)] ;                                \
            if (!A_is_pattern) cast_A (aik, Ax +((pA)*asize), asize)

        // bkj = B(k,j), located in Bx [pB]
        #define GB_GETB(bkj,Bx,pB)                                          \
            GB_void bkj [GB_VLA(bkj_size)] ;                                \
            if (!B_is_pattern) cast_B (bkj, Bx +((pB)*bsize), bsize)

        // t = A(i,k) * B(k,j)
        #define GB_MULT(t, aik, bkj)                                        \
            GB_MULTIPLY (t, aik, bkj)

        // define t for each task
        #define GB_CIJ_DECLARE(t)                                           \
            GB_void t [GB_VLA(csize)]

        // address of Cx [p]
        #define GB_CX(p) (Cx +((p)*csize))

        // Cx [p] = t
        #define GB_CIJ_WRITE(p,t)                                           \
            memcpy (GB_CX (p), t, csize)

        // Cx [p] += t
        #define GB_CIJ_UPDATE(p,t)                                          \
            fadd (GB_CX (p), GB_CX (p), t)

        // address of Hx [i]
        #define GB_HX(i) (Hx +((i)*csize))

        // atomic update not available for function pointers
        #define GB_HAS_ATOMIC 0

        // normal Hx [i] += t
        #define GB_HX_UPDATE(i, t)                                          \
            fadd (GB_HX (i), GB_HX (i), t)

        // normal Hx [i] = t
        #define GB_HX_WRITE(i, t)                                           \
            memcpy (GB_HX (i), t, csize)

        // Cx [p] = Hx [i]
        #define GB_CIJ_GATHER(p,i)                                          \
            memcpy (GB_CX (p), GB_HX(i), csize)

        // memcpy (&(Cx [pC]), &(Hx [i]), len)
        #define GB_CIJ_MEMCPY(pC,i,len) \
            memcpy (Cx +((pC)*csize), Hx +((i)*csize), (len) * csize)

        #define GB_ATYPE GB_void
        #define GB_BTYPE GB_void
        #define GB_CTYPE GB_void

        // printf ("generic saxpy3\n") ;

        if (flipxy)
        { 
            #define GB_MULTIPLY(z,x,y) fmult (z,y,x)
            #include "GB_AxB_saxpy3_template.c"
            #undef GB_MULTIPLY
        }
        else
        { 
            #define GB_MULTIPLY(z,x,y) fmult (z,x,y)
            #include "GB_AxB_saxpy3_template.c"
            #undef GB_MULTIPLY
        }
    }

    //==========================================================================
    // prune empty vectors, free workspace, and return result
    //==========================================================================

    GB_FREE_WORK ;
    ASSERT_MATRIX_OK (C, "saxpy3: C = A*B output", GB0) ;
    info = GB_hypermatrix_prune (C, Context) ;
    ASSERT (*Chandle == C) ;
    ASSERT (!GB_ZOMBIES (C)) ;
    ASSERT (!GB_PENDING (C)) ;
    // printf ("saxpy3 done\n") ;
    return (info) ;
}
