//! \file version.hpp Controls the version of Outcome for cmake, shared library and C++ namespace mangling
#undef BOOST_OUTCOME_VERSION_MAJOR
#undef BOOST_OUTCOME_VERSION_MINOR
#undef BOOST_OUTCOME_VERSION_PATCH
#undef BOOST_OUTCOME_VERSION_REVISION
#undef BOOST_OUTCOME_VERSION_GLUE2
#undef BOOST_OUTCOME_VERSION_GLUE
#undef BOOST_OUTCOME_HEADERS_VERSION
#undef BOOST_OUTCOME_NAMESPACE_VERSION

//! \brief Major version for cmake and DLL version stamping \ingroup config
#define BOOST_OUTCOME_VERSION_MAJOR    1
//! \brief Minor version for cmake and DLL version stamping \ingroup config
#define BOOST_OUTCOME_VERSION_MINOR    0
//! \brief Patch version for cmake and DLL version stamping \ingroup config
#define BOOST_OUTCOME_VERSION_PATCH    0 
//! \brief Revision version for cmake and DLL version stamping \ingroup config
#define BOOST_OUTCOME_VERSION_REVISION 0  // Revision version for cmake and DLL version stamping

//! \brief Defined between stable releases of Outcome. It means the inline namespace
//! will be permuted per-commit to ensure ABI uniqueness. \ingroup config
#define BOOST_OUTCOME_UNSTABLE_VERSION

#define BOOST_OUTCOME_VERSION_GLUE2(a, b, c) a ## b ## c
#define BOOST_OUTCOME_VERSION_GLUE(a, b, c)  BOOST_OUTCOME_VERSION_GLUE2(a, b, c)

#if defined(_MSC_VER) && !defined(__clang__)
#define BOOST_OUTCOME_HEADERS_VERSION     BOOST_OUTCOME_VERSION_GLUE(BOOST_OUTCOME_VERSION_MAJOR, ., BOOST_OUTCOME_VERSION_MINOR)
#else
#define BOOST_OUTCOME_HEADERS_VERSION     BOOST_OUTCOME_VERSION_MAJOR.BOOST_OUTCOME_VERSION_MINOR
#endif