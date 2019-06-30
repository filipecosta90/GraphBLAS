function test117
%TEST117 performance tests for GrB_assign

% test C(:,:)<M> += A

rng ('default') ;
n = 4000 ;

I.begin = 0 ;
I.inc = 1 ;
I.end = n-1 ;

ncores = feature ('numcores') ;

for dc = [1e-5 1e-4 1e-3 1e-2 1e-1 0.5]
    C0 = sprand (n, n, dc) ;

for da = [1e-5 1e-4 1e-3 1e-2 1e-1 0.5]
    A  = sprand (n, n, da) ;

for dm = [1e-5 1e-4 1e-3 1e-2 1e-1 0.5]

    M = spones (sprand (n, n, dm)) ;

    for subset = 0:1

        fprintf ('\n--------------------------------------\n') ;
        fprintf ('dc: %g, da: %g, dm: %g ', dc, da, dm) ;
        if (subset)
            fprintf ('M is a subset of C\n') ;
            M = spones (M.*C0) ;
        end
        fprintf ('\n') ;

        fprintf ('nnz(C): %g million, nnz(M): %g million, ', ...
            nnz (C0) / 1e6, nnz (M) / 1e6) ;
        fprintf ('nnz(A): %g million\n',  nnz (A) / 1e6) ;

        % warmup
        C1 = C0 + M.*A ;

        fprintf ('C(:,:)<M> += A:\n') ;
        tic
        C1 = C0 + M.*A ;
        tm = toc ;

        for nthreads = [1 2 4 8 16 20 32 40 64]
            if (nthreads > 2*ncores)
                break ;
            end
            nthreads_set (nthreads) ;

            % warmup: default method (14d)
            C2 = GB_mex_assign (C0, M, 'plus', A, I, I) ;
            C2 = GB_mex_assign (C0, M, 'plus', A, I, I) ;
            tg = gbresults ;
            assert (isequal (C1, C2.matrix)) ;

            if (nthreads == 1)
                t1 = tg ;
            end

            % nondefault method (6b)
            GB_mex_hack (-1) ;
            C2 = GB_mex_assign (C0, M, 'plus', A, I, I) ;
            C2 = GB_mex_assign (C0, M, 'plus', A, I, I) ;
            GB_mex_hack (0) ;
            tg2 = gbresults ;

            assert (isequal (C1, C2.matrix)) ;

            % ewise
            C2 = GB_mex_eWiseMult_Matrix (C0, [ ], 'plus', 'times', M, A) ;
            C2 = GB_mex_eWiseMult_Matrix (C0, [ ], 'plus', 'times', M, A) ;
            tg3 = gbresults ;

            assert (isequal (C1, C2.matrix)) ;

            fprintf (...
                '%3d : %8.4f GB: %8.4f %8.4f %8.4f rel %8.2f %8.2f %8.2f\n', ...
                nthreads, tm, tg, tg2, tg3, ...
                    tm / tg, tm/tg2, tm/tg3) ;
        end

    end
end
end
end