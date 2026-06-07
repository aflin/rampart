/* vec_blas_probe.h — runtime probe for the BLAS/OpenMP/gfortran chain
 * that the IVFPQ backend (vecindex_ivfpq.cpp + faiss_ivfpq.a) needs.
 *
 * rampart-sql.so is linked with --unresolved-symbols=ignore-in-shared-libs
 * so it can dlopen on a clean install where libopenblas / libgomp /
 * libgfortran / libquadmath aren't present.  HNSW (usearch) needs none
 * of them; only the IVFPQ entry points do.  Every IVFPQ-dispatching site
 * in vecindex.c calls texis_vec_blas_probe() first; if the probe fails,
 * the user gets a clear SQL error ("install libopenblas..." etc.)
 * instead of an opaque dlsym crash at the first faiss::* call.
 *
 * The probe runs at most once per process and caches its result.
 */
#ifndef TEXIS_VEC_BLAS_PROBE_H
#define TEXIS_VEC_BLAS_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if all required libs are loaded (or already-loaded — first
 * call dlopens them with RTLD_GLOBAL).  Returns 0 on failure and sets
 * *err_msg_out to a stable, human-readable string describing which lib
 * is missing.  err_msg_out may be NULL. */
int texis_vec_blas_probe(const char **err_msg_out);

#ifdef __cplusplus
}
#endif

#endif
