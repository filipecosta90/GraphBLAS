function gbtest17
%GBTEST17 test gb.gbtranspose

% SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
% http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

rng ('default') ;

n = 6 ;
m = 7 ;
A = 100 * sprand (n, m, 0.5) ;
AT = A' ;
M = sparse (rand (m,n)) > 0.5 ;
Cin = sprand (m, n, 0.5) ;

Cout = gb.gbtranspose (A) ;
assert (isequal (AT, double (Cout))) ;

Cout = gb.gbtranspose (A) ;
assert (isequal (AT, double (Cout))) ;

Cout = gb.gbtranspose (Cin, M, A) ;
C2 = Cin ;
C2 (M) = AT (M) ;
assert (isequal (C2, double (Cout))) ;

Cout = gb.gbtranspose (Cin, '+', A) ;
C2 = Cin + AT ;
assert (isequal (C2, double (Cout))) ;

d.in0 = 'transpose' ;
Cout = gb.gbtranspose (Cin', M', A, d) ;
C2 = Cin' ;
C2 (M') = A (M') ;
assert (isequal (C2, double (Cout))) ;

Cout = gb.gbtranspose (Cin', '+', A, d) ;
C2 = Cin' + A ;
assert (isequal (C2, double (Cout))) ;

d.mask = 'complement' ;
Cout = gb.gbtranspose (Cin', M', A, d) ;
C2 = Cin' ;
C2 (~M') = A (~M') ;
assert (isequal (C2, double (Cout))) ;

fprintf ('gbtest17: all tests passed\n') ;
