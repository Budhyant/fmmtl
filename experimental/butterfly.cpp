#include <iostream>
#include <complex>

#include <boost/math/special_functions/sin_pi.hpp>
#include <boost/math/special_functions/cos_pi.hpp>
#include <boost/math/constants/constants.hpp>

#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/vector.hpp>
using namespace boost::numeric::ublas;

#include "fmmtl/Kernel.hpp"

#include "fmmtl/numeric/Vec.hpp"
#include "fmmtl/numeric/Complex.hpp"
#include "fmmtl/numeric/norm.hpp"

#include "fmmtl/tree/NDTree.hpp"
#include "fmmtl/tree/TreeRange.hpp"
#include "fmmtl/tree/TreeData.hpp"

#include "fmmtl/executor/Traversal.hpp"

#include "fmmtl/Direct.hpp"


// TODO: Remove dependency of Direct on fmmtl::Kernel
struct FourierKernel : public fmmtl::Kernel<FourierKernel> {
  typedef double value_type;

  typedef Vec<1,value_type> source_type;
  typedef Vec<1,value_type> target_type;
  typedef fmmtl::complex<value_type> charge_type;
  typedef fmmtl::complex<value_type> result_type;

  typedef fmmtl::complex<value_type> kernel_value_type;

  kernel_value_type operator()(const target_type& t,
                               const source_type& s) const {
    const value_type r = 2*inner_prod(t,s);
    return kernel_value_type(boost::math::cos_pi(r), boost::math::sin_pi(r));
  }

  value_type phase(const target_type& t,
                   const source_type& s) const {
    return 2 * boost::math::constants::pi<value_type>() * inner_prod(t,s);
  }

  value_type ampl(const target_type& t,
                  const source_type& s) const {
    return 1;
  }
};


int main(int argc, char** argv) {
  int N = 1000;
  int M = 1000;
  bool checkErrors = true;

  // Parse custom command line args
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i],"-N") == 0) {
      N = atoi(argv[++i]);
    } else if (strcmp(argv[i],"-M") == 0) {
      M = atoi(argv[++i]);
    } else if (strcmp(argv[i],"-nocheck") == 0) {
      checkErrors = false;
    }
  }

  // Define the kernel
  typedef FourierKernel Kernel;
  Kernel K;

  std::cout << KernelTraits<Kernel>() << std::endl;

  typedef typename Kernel::source_type source_type;
  typedef typename Kernel::charge_type charge_type;
  typedef typename Kernel::target_type target_type;
  typedef typename Kernel::result_type result_type;
  typedef typename Kernel::kernel_value_type kernel_value_type;

  // Init sources
  std::vector<source_type> sources(N);
  for (source_type& s : sources)
    s = fmmtl::random<source_type>::get();

  // Init charges
  std::vector<charge_type> charges(N);
  for (charge_type& c : charges)
    c = fmmtl::random<charge_type>::get();

  // Init targets
  std::vector<target_type> targets(M);
  for (target_type& t : targets)
    t = fmmtl::random<target_type>::get();

  // Init results
  std::vector<result_type> result(M);

  // Dimension of the tree
  const unsigned D = fmmtl::dimension<source_type>::value;
  static_assert(D == fmmtl::dimension<target_type>::value, "Dimension mismatch");

  // Construct two trees
  fmmtl::NDTree<D> source_tree(sources, 16);
  fmmtl::NDTree<D> target_tree(targets, 16);

  typedef typename fmmtl::NDTree<D>::box_type target_box_type;
  typedef typename fmmtl::NDTree<D>::box_type source_box_type;
  typedef typename fmmtl::NDTree<D>::body_type target_body_type;
  typedef typename fmmtl::NDTree<D>::body_type source_body_type;

  //
  // Butterfly Application
  //

  // Associate a multipoleAB with each source box
  typedef std::vector<vector<charge_type>> multipole_type;
  auto multipole = make_box_binding<multipole_type>(source_tree);

  // Associate a localAB with each target box
  typedef std::vector<vector<result_type>> local_type;
  auto local = make_box_binding<local_type>(target_tree);

  // The maximum level of interaction
  int max_L = std::min(source_tree.levels(), target_tree.levels()) - 1;

  // Initialize all multipole/local    TODO: improve memory requirements
  for (int L = 0; L <= max_L; ++L) {
    for (source_box_type sbox : boxes(max_L - L, source_tree))
      multipole[sbox].resize(target_tree.boxes(L));
    for (target_box_type tbox : boxes(L, target_tree))
      local[tbox].resize(source_tree.boxes(max_L - L));
  }


  int L_split = max_L / 2;
  assert(L_split > 0);

  // For all levels up to the split
  for (int L = 0; L < L_split; ++L) {

    // For all boxes in the opposing level of the source tree
    int s_idx = -1;
    for (source_box_type sbox : boxes(max_L - L, source_tree)) {
      ++s_idx;

      // For all the boxes in this level of the target tree
      int t_idx = -1;
      for (target_box_type tbox : boxes(L, target_tree)) {
        ++t_idx;

        if (L == 0 || sbox.is_leaf()) {
          // S2M
        } else if (L < L_split) {
          // M2M
        } else {
          // S2L
        }

        if (L == L_split) {
          // M2L
        }

        if (L == max_L || tbox.is_leaf()) {
          // L2T
        } else if (L > L_split) {
          // L2L
        } else {
          // M2T
        }
      }
    }
  }



  // Check the result
  if (checkErrors) {
    std::cout << "Computing direct matvec..." << std::endl;

    // Compute the result with a direct matrix-vector multiplication
    std::vector<result_type> exact(M);
    Direct::matvec(K, sources, charges, targets, exact);

    double tot_error_sq = 0;
    double tot_norm_sq = 0;
    double tot_ind_rel_err = 0;
    double max_ind_rel_err = 0;
    for (unsigned k = 0; k < result.size(); ++k) {
      std::cout << result[k] << "\t" << exact[k] << std::endl;

      // Individual relative error
      double rel_error = norm_2(exact[k] - result[k]) / norm_2(exact[k]);
      tot_ind_rel_err += rel_error;
      // Maximum relative error
      max_ind_rel_err  = std::max(max_ind_rel_err, rel_error);

      // Total relative error
      tot_error_sq += norm_2_sq(exact[k] - result[k]);
      tot_norm_sq  += norm_2_sq(exact[k]);
    }
    double tot_rel_err = sqrt(tot_error_sq/tot_norm_sq);
    std::cout << "Vector  relative error: " << tot_rel_err << std::endl;

    double ave_rel_err = tot_ind_rel_err / result.size();
    std::cout << "Average relative error: " << ave_rel_err << std::endl;

    std::cout << "Maximum relative error: " << max_ind_rel_err << std::endl;
  }

  // TODO: Interpolative decompositions

  // TODO: Range based iterations for tree boxes -- simplification
  // TODO: Generalizations on Context so I can use it in this mock up.
}
