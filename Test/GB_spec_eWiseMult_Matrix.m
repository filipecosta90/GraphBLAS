function C = GB_spec_eWiseMult_Matrix (C, Mask, accum, mult, A, B, descriptor)
%GB_SPEC_EWISEMULT_MATRIX a MATLAB mimic of GrB_eWiseMult_Matrix
%
% Usage:
% C = GB_spec_eWiseMult_Matrix (C, Mask, accum, mult, A, B, descriptor)
%
% Computes C<Mask> = accum(C,T), in GraphBLAS notation, where T =A.*B, A'.*B,
% A.*B' or A'.*B'.  The pattern of T is the union of A and B.

% SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
% http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

%-------------------------------------------------------------------------------
% get inputs
%-------------------------------------------------------------------------------

if (nargout > 1 || nargin ~= 7)
    error ('usage: C = GB_spec_eWiseMult_Matrix (C, Mask, accum, mult, A, B, descriptor)') ;
end

C = GB_spec_matrix (C) ;
A = GB_spec_matrix (A) ;
B = GB_spec_matrix (B) ;
[mult_op optype ztype xtype ytype] = GB_spec_operator (mult, C.class) ;
[C_replace Mask_comp Atrans Btrans Mask_struct] = ...
    GB_spec_descriptor (descriptor) ;
Mask = GB_spec_getmask (Mask, Mask_struct) ;

%-------------------------------------------------------------------------------
% do the work via a clean MATLAB interpretation of the entire GraphBLAS spec
%-------------------------------------------------------------------------------

% apply the descriptor to A
if (Atrans)
    A.matrix = A.matrix.' ;
    A.pattern = A.pattern' ;
end

% apply the descriptor to B
if (Btrans)
    B.matrix = B.matrix.' ;
    B.pattern = B.pattern' ;
end

% T = A.*B, with typecasting
T.matrix = GB_spec_zeros (size (A.matrix), ztype) ;

% apply the mult to entries in the intersection of A and B
p = A.pattern & B.pattern ;
% first cast the entries into the class of the operator
A1 = GB_mex_cast (A.matrix (p), xtype) ;
B1 = GB_mex_cast (B.matrix (p), ytype) ;
T.matrix (p) = GB_spec_op (mult, A1, B1) ;

% the pattern of T is the intersection of both A and B
T.pattern = p ;

assert (isequal (ztype, GB_spec_type (T.matrix))) ;
T.class = ztype ;

% C<Mask> = accum (C,T): apply the accum, then Mask, and return the result
C = GB_spec_accum_mask (C, Mask, accum, T, C_replace, Mask_comp, 0) ;


