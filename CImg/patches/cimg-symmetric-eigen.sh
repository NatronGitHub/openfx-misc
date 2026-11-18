#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the eigenvectors
# of rank-deficient symmetric matrices are not NaN. Usage: cimg-symmetric-eigen.sh <CImg.h>
#
# CImg<T>::symmetric_eigen() (the 'eigen' command, and eig() in the math parser) calls SVD(), which
# breaks down on rank-deficient matrices when the compiler contracts floating-point expressions
# into fused multiply-adds (as Apple clang does on arm64, or -O2 -march=native on x86-64): e.g.
# '9,9,1,1,1 eigen' gives NaN eigenvectors. Smooth [Patch-PCA] (denoise_patchpca) then outputs
# NaN pixels near edges and on ramps, where its patch correlation matrices are rank-deficient.
# https://github.com/GreycLab/gmic/issues/221, NatronGitHub/openfx-gmic#71.
#
# Upstream fixed it after G'MIC 4.0.7, in GreycLab/CImg e3de7b0 (develop): symmetric_eigen() uses
# the cyclic Jacobi method instead of SVD(), and SVD() copies its input with assign() and clamps
# a sum of squares to 0 before its square root. This script backports that commit verbatim: the
# whole of symmetric_eigen() (with its comment), and the three changed lines of SVD(). Upstream
# then rewrote SVD() in double precision, in GreycLab/CImg c9eb6fa (develop,
# https://github.com/GreycLab/CImg/commit/c9eb6fa661a1af948c6845dabaeaf0ebef101f96), which removes
# these three lines; symmetric_eigen() is the same as in e3de7b0, and no longer calls SVD().
#
# "If the fix is already there, don't apply it": the script does nothing on a CImg.h that has the
# fix (patched, fixed upstream as in e3de7b0, or as in c9eb6fa: the symmetric_eigen() of e3de7b0
# and the SVD() of c9eb6fa, recognized by its first comment line, and none of the three old or
# fixed lines), applies it to the 4.0.7 code, and fails on anything else (e.g. after a GMICVER
# bump that changed this code otherwise): then check what upstream did, and update or remove this
# patch. Lines are compared without their leading and trailing blanks. Only POSIX sh, awk and wc
# are used, for Linux, macOS and MSYS2.
set -e
f="$1"

# the changed lines of SVD(), before and after the fix (the second one occurs twice)
svd1_old='U = *this;'
svd1_new='U.assign(*this,false);'
svd2_old='g = (Ttfloat)((f>=0?-1:1)*std::sqrt(s));'
svd2_new='g = (Ttfloat)((f>=0?-1:1)*std::sqrt(std::max((Tfloat)0,s)));'
# the first comment line of SVD() as rewritten in GreycLab/CImg c9eb6fa
svd_c9eb6fa='// Working copy in double (a will become U), V, singular values w and superdiagonal rv1.'

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-symmetric-eigen.sh (GreycLab/gmic#221)" >&2
  rm -f "$f.patched" "$f.eigen_old" "$f.eigen_new"
  exit 1
}

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

# symmetric_eigen() in G'MIC 4.0.7's CImg.h
cat > "$f.eigen_old" <<'END_OF_OLD'
    //! Compute eigenvalues and eigenvectors of the instance image, viewed as a symmetric matrix.
    /**
       \param[out] val Vector of the estimated eigenvalues, in decreasing order.
       \param[out] vec Matrix of the estimated eigenvectors, sorted by columns.
    **/
    template<typename t>
    const CImg<T>& symmetric_eigen(CImg<t>& val, CImg<t>& vec) const {
      if (is_empty()) { val.assign(); vec.assign(); return *this; }
      if (_width!=_height || _depth>1 || _spectrum>1)
        throw CImgInstanceException(_cimg_instance
                                    "eigen(): Instance is not a square matrix.",
                                    cimg_instance);
      val.assign(1,_width);
      vec.assign(_width,_width);

      if (_width==1) { val[0] = cimg::abs((*this)[0]); vec[0] = 1; return *this; }
      if (_width==2) {
        const double
          a = (*this)[0], b = (*this)[1], c = (*this)[2], d = (*this)[3],
          e = a + d, f = std::sqrt(std::max(e*e - 4*(a*d - b*c),0.0)),
          l1 = 0.5*(e - f), l2 = 0.5*(e + f),
          n = std::sqrt(cimg::sqr(l2 - a) + b*b);
        val[0] = (t)l2;
        val[1] = (t)l1;
        if (n>0) { vec[0] = (t)(b/n); vec[2] = (t)((l2 - a)/n); } else { vec[0] = 1; vec[2] = 0; }
        vec[1] = -vec[2];
        vec[3] = vec[0];
        return *this;
      }
      CImg<t> V(_width,_width);
      Tfloat M = 0, m = (Tfloat)min_max(M), maxabs = cimg::max((Tfloat)1,cimg::abs(m),cimg::abs(M));
      (CImg<Tfloat>(*this,false)/=maxabs).SVD(vec,val,V,false);
      if (maxabs!=1) val*=maxabs;

      // Retrieve signed eigenvalues (compute X^t.A.X, which is more robust than using sign(U_k.V_k)).
      cimg_forY(val,p) {
        const CImg<Tfloat> u = vec.get_column(p);
        val[p] = u.dot((*this)*u);
      }

      // Sort eigenvalues in decreasing order
      CImg<intT> permutations;
      CImg<t> tmp(_width);
      val.sort(permutations,false);
      cimg_forY(vec,k) {
        cimg_forY(permutations,y) tmp(y) = vec(permutations(y),k);
        std::memcpy(vec.data(0,k),tmp._data,sizeof(t)*_width);
      }
      return *this;
    }
END_OF_OLD
# symmetric_eigen() as fixed upstream (GreycLab/CImg e3de7b0)
cat > "$f.eigen_new" <<'END_OF_NEW'
    //! Compute eigenvalues and eigenvectors of a symmetric matrix (using cyclic Jacobi algorithm).
    /**
       \param val Eigenvalues, sorted in decreasing order (column vector, size 1xn).
       \param vec Eigenvectors, stored column by column: vec.get_column(i) is the
       unit eigenvector associated with val[i].
    **/
    template<typename t>
    const CImg<T>& symmetric_eigen(CImg<t>& val, CImg<t>& vec) const {
      if (is_empty()) { val.assign(); vec.assign(); return *this; }
      if (_width!=_height || _depth>1 || _spectrum>1)
        throw CImgInstanceException(_cimg_instance
                                    "symmetric_eigen(): Instance is not a square matrix.",
                                    cimg_instance);
      const int n = (int)_width;

      // Work in double precision on the symmetrized matrix. a[i*n+j] = row i, column j.
      CImg<doubleT> A(n,n), V(n,n,1,1,0);
      double *const a = A._data, *const v = V._data;
      for (int i = 0; i<n; ++i) {
        for (int j = 0; j<n; ++j)
          a[i*n + j] = 0.5*((double)(*this)(j,i) + (double)(*this)(i,j));
        v[i*n + i] = 1;
      }

      double frob2 = 0;
      for (int i = 0; i<n*n; ++i) frob2 += a[i]*a[i];
      const double tol = 1e-32*frob2;   // ~ (DBL_EPSILON)^2 * ||A||_F^2

      for (int sweep = 0; sweep<100; ++sweep) {
        double off = 0;
        for (int i = 1; i<n; ++i) for (int j = 0; j<i; ++j) off += a[i*n + j]*a[i*n + j];
        if (off<=tol) break;

        for (int p = 0; p<n - 1; ++p) for (int q = p + 1; q<n; ++q) {
            const double apq = a[p*n + q];
            if (apq==0) continue;
            const double app = a[p*n + p], aqq = a[q*n + q],
              theta = (aqq - app)/(2*apq),
              tt = cimg::abs(theta)>1e150 ? 0.5/theta :
              (theta>=0?1.:-1.)/(cimg::abs(theta) + std::sqrt(theta*theta + 1)),
              c = 1/std::sqrt(tt*tt + 1),
              s = tt*c;

            // A <- A.J (columns p and q).
            for (int k = 0; k<n; ++k) {
              const double akp = a[k*n + p], akq = a[k*n + q];
              a[k*n + p] = c*akp - s*akq;
              a[k*n + q] = s*akp + c*akq;
            }
            // A <- J^T.A (rows p and q).
            for (int k = 0; k<n; ++k) {
              const double apk = a[p*n + k], aqk = a[q*n + k];
              a[p*n + k] = c*apk - s*aqk;
              a[q*n + k] = s*apk + c*aqk;
            }
            // Overwrite the 2x2 block with its exact analytical values.
            a[p*n + p] = app - tt*apq;
            a[q*n + q] = aqq + tt*apq;
            a[p*n + q] = a[q*n + p] = 0;

            // V <- V.J (accumulate eigenvectors as columns).
            for (int k = 0; k<n; ++k) {
              const double vkp = v[k*n + p], vkq = v[k*n + q];
              v[k*n + p] = c*vkp - s*vkq;
              v[k*n + q] = s*vkp + c*vkq;
            }
          }
      }

      // Sort eigenvalues in decreasing order (selection sort on a permutation).
      CImg<intT> perm(n);
      for (int i = 0; i<n; ++i) perm[i] = i;
      for (int i = 0; i<n - 1; ++i) {
        int m = i;
        for (int j = i + 1; j<n; ++j) if (a[perm[j]*n + perm[j]]>a[perm[m]*n + perm[m]]) m = j;
        cimg::swap(perm[i],perm[m]);
      }

      val.assign(1,n);
      vec.assign(n,n);
      for (int i = 0; i<n; ++i) {
        val[i] = (t)a[perm[i]*n + perm[i]];
        for (int k = 0; k<n; ++k) vec(i,k) = (t)v[k*n + perm[i]];  // column i = eigenvector i
      }
      return *this;
    }
END_OF_NEW

# count <file> <block file>: number of times the lines of <block file> occur in a row in <file>
count() {
  awk -v blk="$2" '
    function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
    BEGIN { while ((getline l < blk) > 0) b[++m] = trim(l) }
    { t[++n] = trim($0) }
    END {
      for (i = 1; i + m - 1 <= n; ++i) {
        if (t[i] != b[1]) continue
        for (j = 2; j <= m && t[i + j - 1] == b[j]; ++j) ;
        if (j > m) ++c
      }
      print c + 0
    }' "$1"
}
# lines <file> <line>: number of lines equal to <line>
lines() {
  awk -v l="$2" '{ gsub(/^[ \t]+|[ \t\r]+$/, "") } $0 == l { ++c } END { print c + 0 }' "$1"
}
# state <file>: how many times the old symmetric_eigen() and SVD() lines occur, then the new ones,
# then the first comment line of the SVD() of c9eb6fa
state() {
  echo "$(count "$1" "$f.eigen_old") $(lines "$1" "$svd1_old") $(lines "$1" "$svd2_old")" \
    "$(count "$1" "$f.eigen_new") $(lines "$1" "$svd1_new") $(lines "$1" "$svd2_new")" \
    "$(lines "$1" "$svd_c9eb6fa")"
}

found=$(state "$f")
# already patched, or fixed upstream as in e3de7b0: nothing to do
if [ "$found" = "0 0 0 1 1 2 0" ]; then
  rm -f "$f.eigen_old" "$f.eigen_new"
  echo "$f: already patched (GreycLab/gmic#221)"
  exit 0
fi
# fixed upstream as in c9eb6fa: nothing to do
if [ "$found" = "0 0 0 1 0 0 1" ]; then
  rm -f "$f.eigen_old" "$f.eigen_new"
  echo "$f: already fixed upstream (GreycLab/gmic#221, GreycLab/CImg c9eb6fa)"
  exit 0
fi
# the code to patch must be found exactly
[ "$found" = "1 1 2 0 0 0 0" ] || fail "expected symmetric_eigen() once and the lines '$svd1_old' once and '$svd2_old' twice, as in G'MIC 4.0.7, or the same as fixed in GreycLab/CImg e3de7b0, or the symmetric_eigen() of e3de7b0 with the SVD() of GreycLab/CImg c9eb6fa (found, old, fixed, then the SVD() of c9eb6fa: $found)"

awk -v old="$f.eigen_old" -v new="$f.eigen_new" -v s1o="$svd1_old" -v s1n="$svd1_new" -v s2o="$svd2_old" -v s2n="$svd2_new" '
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  BEGIN {
    while ((getline l < old) > 0) b[++m] = trim(l)
    while ((getline l < new) > 0) r[++k] = l
  }
  { o[++n] = $0; t[n] = trim($0) }
  END {
    for (i = 1; i <= n; ++i) {
      if (t[i] == b[1]) {
        for (j = 2; j <= m && t[i + j - 1] == b[j]; ++j) ;
        if (j > m) { for (j = 1; j <= k; ++j) print r[j]; i += m - 1; continue }
      }
      if (t[i] == s1o || t[i] == s2o) {
        match(o[i], /^[ \t]*/)
        print substr(o[i], 1, RLENGTH) (t[i] == s1o ? s1n : s2n)
        continue
      }
      print o[i]
    }
  }' "$f" > "$f.patched" || fail "awk failed"
[ "$(state "$f.patched")" = "0 0 0 1 1 2 0" ] || fail "the patch did not apply"
[ "$(wc -l < "$f.patched")" -eq $(($(wc -l < "$f") - $(wc -l < "$f.eigen_old") + $(wc -l < "$f.eigen_new"))) ] ||
  fail "the patch changed the number of lines unexpectedly"
rm -f "$f.eigen_old" "$f.eigen_new"
mv "$f.patched" "$f"
echo "$f: patched for NaN eigenvectors of rank-deficient symmetric matrices (GreycLab/gmic#221)"
