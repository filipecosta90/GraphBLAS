function C = max (varargin)
%MAX Maximum elements of a GraphBLAS or MATLAB matrix.
%
% C = max (G) is the largest entry in the vector G.  If G is a matrix,
% C is a row vector with C(j) = max (G (:,j)).
%
% C = max (A,B) is an array of the element-wise maximum of two matrices
% A and B, which either have the same size, or one can be a scalar.
% Either A and/or B can be GraphBLAS or MATLAB matrices.
%
% C = max (G, [ ], 'all') is a scalar, with the largest entry in G.
% C = max (G, [ ], 1) is a row vector with C(j) = max (G (:,j))
% C = max (G, [ ], 2) is a column vector with C(i) = max (G (i,:))
%
% The indices of the maximum entry, or [C,I] = max (...) in the MATLAB
% built-in max function, are not computed.  The max (..., nanflag) option
% is not available; only the 'includenan' behavior is supported.
%
% See also min.

% SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
% http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

G = varargin {1} ;
[m n] = size (G) ;
if (isequal (gb.type (G), 'logical'))
    op = '|.logical' ;
else
    op = 'max' ;
end

if (nargin == 1)

    % C = max (G)
    if (isvector (G))
        % C = max (G) for a vector G results in a scalar C
        C = gb.reduce (op, G) ;
        if (gb.nvals (G) < m*n) ;
            C = max (C, 0) ;
        end
    else
        % C = max (G) reduces each column to a scalar,
        % giving a 1-by-n row vector.
        C = gb.vreduce (op, G, struct ('in0', 'transpose')) ;
        % if C(j) < 0, but the column is sparse, then assign C(j) = 0.
        C = gb.subassign (C, (C < 0) & (gb.coldegree (G) < m), 0)' ;
    end

elseif (nargin == 2)

    % C = max (A,B)
    A = varargin {1} ;
    B = varargin {2} ;
    if (isscalar (A))
        if (isscalar (B))
            % both A and B are scalars.  Result is also a scalar.
            C = sparse_comparator (op, A, B) ;
        else
            % A is a scalar, B is a matrix
            if (get_scalar (A) > 0)
                % since A > 0, the result is full
                A = gb.expand (A, true (size (B))) ;
            else
                % since A <= 0, the result is sparse.  Expand the scalar A
                % to the pattern of B.
                A = gb.expand (A, B) ;
            end
            C = gb.eadd (op, A, B) ;
        end
    else
        if (isscalar (B))
            % A is a matrix, B is a scalar
            if (get_scalar (B) > 0)
                % since B > 0, the result is full
                B = gb.expand (B, true (size (A))) ;
            else
                % since B <= 0, the result is sparse.  Expand the scalar B
                % to the pattern of A.
                B = gb.expand (B, A) ;
            end
            C = gb.eadd (op, A, B) ;
        else
            % both A and B are matrices.  Result is sparse.
            C = sparse_comparator (op, A, B) ;
        end
    end

elseif (nargin == 3)

    % C = max (G, [ ], option)
    option = varargin {3} ;
    if (isequal (option, 'all'))
        % C = max (G, [ ] 'all'), reducing all entries to a scalar
        C = gb.reduce (op, G) ;
        if (gb.nvals (G) < m*n) ;
            C = max (C, 0) ;
        end
    elseif (isequal (option, 1))
        % C = max (G, [ ], 1) reduces each column to a scalar,
        % giving a 1-by-n row vector.
        C = gb.vreduce (op, G, struct ('in0', 'transpose')) ;
        % if C(j) < 0, but the column is sparse, then assign C(j) = 0.
        C = gb.subassign (C, (C < 0) & (gb.coldegree (G) < m), 0)' ;
    elseif (isequal (option, 2))
        % C = max (G, [ ], 2) reduces each row to a scalar,
        % giving an m-by-1 column vector.
        C = gb.vreduce (op, G) ;
        % if C(i) < 0, but the row is sparse, then assign C(i) = 0.
        C = gb.subassign (C, (C < 0) & (gb.rowdegree (G) < n), 0) ;
    else
        error ('unknown option') ;
    end

else
    error ('invalid usage') ;
end

