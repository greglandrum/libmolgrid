/*
 * grid_maker.h
 *
 *  Grid generation form atomic data.
 *  Created on: Mar 26, 2019
 *      Author: dkoes
 */

#ifndef GRID_MAKER_H_
#define GRID_MAKER_H_

#include <vector>
#include <array>
#include <algorithm>
#include <cuda_runtime.h>
#include "libmolgrid/coordinateset.h"
#include "libmolgrid/grid.h"
#include "libmolgrid/example.h"
#include "libmolgrid/transform.h"

namespace libmolgrid {

/**
 * \class GridMaker
 * Populates a grid with atom density values that correspond to atoms in a
 * CoordinateSet and accumulates atomic gradients from the grid gradients.
 * It stores state about universal grid settings. In functions that map from
 * atomic coordinates to grids and vice versa (e.g. forward and backward), it
 * must be passed the grid_center (which may have changed due to
 * transformations performed directly on the atom coordinates externally to
 * this class)
 */
class GridMaker {
  protected:
    float resolution = 0.5; /// grid spacing
    float dimension = 0; /// grid side length in Angstroms
    float radius_scale = 1.0; ///pre-multiplier for radius; simplest way to change size of atoms
    float gaussian_radius_multiple = 1.0; /// multiple of atomic radius that gaussian function extends to
    ///this is not set by the user, for G=gaussian_radius_multiple this is
    /// $\frac{1+2G^2}{2G}$
    float final_radius_multiple = 1.5;

    float A,B,C; //precalculated coefficients for density
    float D,E; //precalculate coefficients for backprop
    bool binary; /// use binary occupancy instead of real-valued atom density
    unsigned dim; /// grid width in points

    template<typename Dtype, bool isCUDA>
    void check_index_args(const Grid<float, 2, isCUDA>& coords,
        const Grid<float, 1, isCUDA>& type_index, const Grid<float, 1, isCUDA>& radii,
        Grid<Dtype, 4, isCUDA>& out) const;

    template<typename Dtype, bool isCUDA>
    void check_vector_args(const Grid<float, 2, isCUDA>& coords,
        const Grid<float, 2, isCUDA>& type_vector, const Grid<float, 1, isCUDA>& radii,
        Grid<Dtype, 4, isCUDA>& out) const;
  public:

    GridMaker(float res = 0, float d = 0, bool bin = false, float rscale=1.0, float grm = 1.0) :
      resolution(res), dimension(d), radius_scale(rscale), gaussian_radius_multiple(grm), final_radius_multiple(0), binary(bin) {
        initialize(res, d, bin, rscale, grm);
      }

    virtual ~GridMaker() {}

    /** \brief Initialize grid settings
     * @param[in] res resolution of grid in Angstroms
     * @param[in] d dimension of cubic grid side in Angstroms
     * @param[in] bin boolean indicating if binary density should be used
     * @param[in] rscale scaling factor to be uniformly applied to all input radii
     * @param[in] grm gaussian radius multiplier - cutoff point for switching from Gaussian density to quadratic
     */
    void initialize(float res, float d, bool bin = false, float rscale=1.0, float grm=1.0);

    ///return spatial dimensions of grid
    float3 get_grid_dims() const {
      return make_float3(dim, dim, dim);
    }

    ///return resolution in Angstroms
    CUDA_CALLABLE_MEMBER float get_resolution() const { return resolution; }

    ///set resolution in Angstroms
    CUDA_CALLABLE_MEMBER void set_resolution(float res) { resolution = res; dim = ::round(dimension / resolution) + 1; }

    ///get dimension in Angstroms
    CUDA_CALLABLE_MEMBER float get_dimension() const { return dimension; }
    ///set dimension in Angstroms
    CUDA_CALLABLE_MEMBER void set_dimension(float d) { dimension = d; dim = ::round(dimension / resolution) + 1; }

    CUDA_CALLABLE_MEMBER unsigned get_first_dim() const { return dim; }

    ///return if density is binary
    CUDA_CALLABLE_MEMBER bool get_binary() const { return binary; }
    ///set if density is binary
    CUDA_CALLABLE_MEMBER void set_binary(bool b) { binary = b; }

    ///return multiplier of radius where density goes to zero
    CUDA_CALLABLE_MEMBER float get_radiusmultiple() const { return radius_scale*final_radius_multiple; }

    /** \brief Use externally specified grid_center to determine where grid begins.
     * Used for translating between cartesian coords and grids.
     * @param[in] grid center
     * @param[out] grid bounds
     */
    CUDA_CALLABLE_MEMBER float3 get_grid_origin(const float3& grid_center) const;

    /* \brief Generate grid tensor from atomic data.  Grid (CPU) must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinate set
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const CoordinateSet& in, Grid<Dtype, 4, false>& out) const {
      if(in.has_indexed_types()) {
        forward(grid_center, in.coords.cpu(), in.type_index.cpu(), in.radii.cpu(), out);
      } else {
        forward(grid_center, in.coords.cpu(), in.type_vector.cpu(), in.radii.cpu(), out);
      }
    }

    /* \brief Generate grid tensor from atomic data.  Grid (GPU) must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinate set
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const CoordinateSet& in, Grid<Dtype, 4, true>& out) const {
      if(in.has_indexed_types()) {
        forward(grid_center, in.coords.gpu(), in.type_index.gpu(), in.radii.gpu(), out);
      } else {
        forward(grid_center, in.coords.gpu(), in.type_vector.gpu(), in.radii.gpu(), out);
      }
    }

    /* \brief Generate grid tensor from an example while applying a transformation.
     * The center specified in the transform will be used as the grid center.
     *
     * @param[in] ex example
     * @param[in] transform transformation to apply
     * @param[out] out a 4D grid
     */
    template <typename Dtype, bool isCUDA>
    void forward(const Example& in, const Transform& transform, Grid<Dtype, 4, isCUDA>& out) const;

    /* \brief Generate grid tensor from an example.
     * Coordinates may be optionally translated/rotated.  Do not use this function
     * if it is desirable to retain the transformation used (e.g., when backpropagating).
     *
     * @param[in] ex example
     * @param[in] transform transformation to apply
     * @param[out] out a 4D grid
     * @param[in] random_translation  maximum amount to randomly translate each coordinate (+/-)
     * @param[in] random_rotation whether or not to randomly rotate
     * @param[in] center grid center to use, if not provided will use center of the last coordinate set before transformation
     */
    template <typename Dtype, bool isCUDA>
    void forward(const Example& in, Grid<Dtype, 4, isCUDA>& out,
        float random_translation=0.0, bool random_rotation = false,
        const float3& center = make_float3(INFINITY, INFINITY, INFINITY)) const;

    /* \brief Generate grid tensor from a vector of examples, as provided by ExampleProvider.next_batch.
     * Coordinates may be optionally translated/rotated.  Do not use this function
     * if it is desirable to retain the transformation used (e.g., when backpropagating).
     * The center of the last coordinate set before transformation
     * will be used as the grid center.
     *
     * @param[in] ex example
     * @param[in] transform transformation to apply
     * @param[out] out a 4D grid
     * @param[in] random_translation  maximum amount to randomly translate each coordinate (+/-)
     * @param[in] random_rotation whether or not to randomly rotate
     */
    template <typename Dtype, bool isCUDA>
    void forward(const std::vector<Example>& in, Grid<Dtype, 5, isCUDA>& out, float random_translation=0.0, bool random_rotation = false) const {
      if(in.size() != out.dimension(0)) throw std::out_of_range("output grid dimension does not match size of example vector");
      for(unsigned i = 0, n = in.size(); i < n; i++) {
        Grid<Dtype, 4, isCUDA> g(out[i]);
        forward<Dtype,isCUDA>(in[i],g, random_translation, random_rotation);
      }
    }


    /* \brief Generate grid tensor from CPU atomic data.  Grid must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type indices (N integers stored as floats)
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const Grid<float, 2, false>& coords,
        const Grid<float, 1, false>& type_index, const Grid<float, 1, false>& radii,
        Grid<Dtype, 4, false>& out) const;

    /* \brief Generate grid tensor from GPU atomic data.  Grid must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type indices (N integers stored as floats)
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const Grid<float, 2, true>& coords,
        const Grid<float, 1, true>& type_index, const Grid<float, 1, true>& radii,
        Grid<Dtype, 4, true>& out) const;
        
        
    /* \brief Generate grid tensor from CPU atomic data.  Grid must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type vectors (NxT)
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const Grid<float, 2, false>& coords,
        const Grid<float, 2, false>& type_vector, const Grid<float, 1, false>& radii,
        Grid<Dtype, 4, false>& out) const;

    /* \brief Generate grid tensor from GPU atomic data.  Grid must be properly sized.
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type indices (NxT)
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype>
    void forward(float3 grid_center, const Grid<float, 2, true>& coords,
        const Grid<float, 2, true>& type_vector, const Grid<float, 1, true>& radii,
        Grid<Dtype, 4, true>& out) const;        


    /* \brief Generate atom and type gradients from grid gradients. (CPU)
     * Must provide atom coordinates that defined the original grid in forward
     * Vector types are required.
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     * @param[out] type_gradients only set if input has type vectors
     */
    template <typename Dtype>
    void backward(float3 grid_center, const CoordinateSet& in, const Grid<Dtype, 4, false>& diff,
        Grid<Dtype, 2, false>& atomic_gradients, Grid<Dtype, 2, false>& type_gradients) const {
      if(in.has_vector_types()) {
        backward(grid_center, in.coords.cpu(), in.type_vector.cpu(), in.radii.cpu(), diff, atomic_gradients, type_gradients);
      } else {
        throw std::invalid_argument("Vector types missing from coordinate set");
      }
    }

    /* \brief Generate atom gradients from grid gradients. (CPU)
     * Must provide atom coordinates that defined the original grid in forward
     * Index types are required
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     */
    template <typename Dtype>
    void backward(float3 grid_center, const CoordinateSet& in, const Grid<Dtype, 4, false>& diff,
        Grid<Dtype, 2, false>& atomic_gradients) const {
      if(in.has_indexed_types()) {
        backward(grid_center, in.coords.cpu(), in.type_index.cpu(), in.radii.cpu(), diff, atomic_gradients);
      } else {
        throw std::invalid_argument("Index types missing from coordinate set"); //could setup dummy types here
      }
    }

    /* \brief Generate atom and type gradients from grid gradients. (GPU)
     * Must provide atom coordinates that defined the original grid in forward
     * Vector types are required.
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     * @param[out] type_gradients only set if input has type vectors
     */
    template <typename Dtype>
    void backward(float3 grid_center, const CoordinateSet& in, const Grid<Dtype, 4, true>& diff,
        Grid<Dtype, 2, true>& atomic_gradients, Grid<Dtype, 2, true>& type_gradients) const {
      if(in.has_vector_types()) {
        backward(grid_center, in.coords.gpu(), in.type_vector.gpu(), in.radii.gpu(), diff, atomic_gradients, type_gradients);
      } else {
        throw std::invalid_argument("Vector types missing from coordinate set");
      }
    }

    /* \brief Generate atom gradients from grid gradients. (GPU)
     * Must provide atom coordinates that defined the original grid in forward
     * Index types are required.
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     */
    template <typename Dtype>
    void backward(float3 grid_center, const CoordinateSet& in, const Grid<Dtype, 4, true>& diff,
        Grid<Dtype, 2, true>& atomic_gradients) const {
      if(in.has_indexed_types()) {
        backward(grid_center, in.coords.gpu(), in.type_index.gpu(), in.radii.gpu(), diff, atomic_gradients);
      } else {
        throw std::invalid_argument("Index types missing from coordinate set");
      }
    }

    /* \brief Generate atom gradients from grid gradients. (CPU)
     * Must provide atom coordinates, types, and radii that defined the original grid in forward
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type indices (N integers stored as floats)
     * @param[in] radii (N)
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     */
    template <typename Dtype>
    void backward(float3 grid_center, const Grid<float, 2, false>& coords,
        const Grid<float, 1, false>& type_index, const Grid<float, 1, false>& radii,
        const Grid<Dtype, 4, false>& diff, Grid<Dtype, 2, false>& atom_gradients) const;

    /* \brief Generate atom gradients from grid gradients. (GPU)
     * Must provide atom coordinates, types, and radii that defined the original grid in forward
     * @param[in] center of grid
     * @param[in] coordinates(Nx3)
     * @param[in] type indices (N integers stored as floats)
     * @param[in] radii (N)
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     */
    template <typename Dtype>
    void backward(float3 grid_center, const Grid<float, 2, true>& coords,
        const Grid<float, 1, true>& type_index, const Grid<float, 1, true>& radii,
        const Grid<Dtype, 4, true>& grid, Grid<Dtype, 2, true>& atom_gradients) const;

    /* \brief Generate atom and type gradients from grid gradients. (CPU)
     * Must provide atom coordinates, types, and radii that defined the original grid in forward
     * @param[in] center of grid
     * @param[in] coordinates  (Nx3)
     * @param[in] type vectors (NxT)
     * @param[in] radii (N)
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     * @param[out] type_gradients vector quantities for each atom
     */
    template <typename Dtype>
    void backward(float3 grid_center, const Grid<float, 2, false>& coords,
        const Grid<float, 2, false>& type_vectors, const Grid<float, 1, false>& radii,
        const Grid<Dtype, 4, false>& diff,
        Grid<Dtype, 2, false>& atom_gradients, Grid<Dtype, 2, false>& type_gradients) const;

    /* \brief Generate atom gradients from grid gradients. (GPU)
     * Must provide atom coordinates, types, and radii that defined the original grid in forward
     * @param[in] center of grid
     * @param[in] coordinates (Nx3)
     * @param[in] type vectors (NxT)
     * @param[in] radii (N)
     * @param[in] diff a 4D grid of gradients
     * @param[out] atomic_gradients vector quantities for each atom
     * @param[out] type_gradients vector quantities for each atom
     *
     */
    template <typename Dtype>
    void backward(float3 grid_center, const Grid<float, 2, true>& coords,
        const Grid<float, 2, true>& type_vectors, const Grid<float, 1, true>& radii,
        const Grid<Dtype, 4, true>& grid,
        Grid<Dtype, 2, true>& atom_gradients,  Grid<Dtype, 2, true>& type_gradients) const;

    /* \brief Propagate relevance (in diff) onto atoms. (CPU)
     * Index types are required.
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] density a 4D grid of densities (used in forward)
     * @param[in] diff a 4D grid of relevance
     * @param[out] relevance score for each atom
     */
    template <typename Dtype>
    void backward_relevance(float3 grid_center, const CoordinateSet& in,
        const Grid<Dtype, 4, false>& density, const Grid<Dtype, 4, false>& diff,
        Grid<Dtype, 1, false>& relevance) const {
      if(in.has_indexed_types()) {
        backward_relevance(grid_center, in.coords.cpu(), in.type_index.cpu(), in.radii.cpu(), density, diff, relevance);
      } else {
        throw std::invalid_argument("Index types missing from coordinate set in backward relevance"); //could setup dummy types here
      }
    }

    /* \brief Propagate relevance (in diff) onto atoms. (GPU)
     * Index types are required.
     * @param[in] center of grid
     * @param[in] in coordinate set
     * @param[in] density a 4D grid of densities (used in forward)
     * @param[in] diff a 4D grid of relevance
     * @param[out] relevance score for each atom
     */
    template <typename Dtype>
    void backward_relevance(float3 grid_center,  const CoordinateSet& in,
        const Grid<Dtype, 4, true>& density, const Grid<Dtype, 4, true>& diff,
        Grid<Dtype, 1, true>& relevance) const {
      if(in.has_indexed_types()) {
        backward_relevance(grid_center, in.coords.gpu(), in.type_index.gpu(), in.radii.gpu(), density, diff, relevance);
      } else {
        throw std::invalid_argument("Index types missing from coordinate set in backward relevance"); //could setup dummy types here
      }
    }

    /* \brief Propagate relevance (in diff) onto atoms. (CPU)
     * Index types are required.
     * @param[in] center of grid
     * @param[in] coords coordinates
     * @param[in] type_index
     * @param[in] radii
     * @param[in] density a 4D grid of densities (used in forward)
     * @param[in] diff a 4D grid of relevance
     * @param[out] relevance score for each atom
     */
    template <typename Dtype>
    void backward_relevance(float3 grid_center,  const Grid<float, 2, false>& coords,
        const Grid<float, 1, false>& type_index, const Grid<float, 1, false>& radii,
        const Grid<Dtype, 4, false>& density, const Grid<Dtype, 4, false>& diff,
        Grid<Dtype, 1, false>& relevance) const;

    /* \brief Propagate relevance (in diff) onto atoms. (GPU)
     * Index types are required.
     * @param[in] center of grid
     * @param[in] coords coordinates
     * @param[in] type_index
     * @param[in] radii
     * @param[in] density a 4D grid of densities (used in forward)
     * @param[in] diff a 4D grid of relevance
     * @param[out] relevance score for each atom
     */
    template <typename Dtype>
    void backward_relevance(float3 grid_center,  const Grid<float, 2, true>& coords,
        const Grid<float, 1, true>& type_index, const Grid<float, 1, true>& radii,
        const Grid<Dtype, 4, true>& density, const Grid<Dtype, 4, true>& diff,
        Grid<Dtype, 1, true>& relevance) const;


    /* \brief The function that actually updates the voxel density values.
     * @param[in] number of possibly relevant atoms
     * @param[in] grid origin
     * @param[in] coordinates
     * @param[in] type indices (N integers stored as floats)
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype, bool Binary>
    CUDA_DEVICE_MEMBER void set_atoms(unsigned natoms, float3 grid_origin,
        const float3 *coords, const float *tindex, const float *radii, Dtype* out);

    /* \brief The function that actually updates the voxel density values.
     * @param[in] number of possibly relevant atoms
     * @param[in] grid origin
     * @param[in] coordinates
     * @param[in] type vector (NxT)
     * @param[in] ntypes number of types
     * @param[in] radii (N)
     * @param[out] a 4D grid
     */
    template <typename Dtype, bool Binary>
    CUDA_DEVICE_MEMBER void set_atoms(unsigned natoms, float3 grid_origin,
        const float3 *coords, const float *type_vec, unsigned ntypes,
        const float *radii, Dtype* out);

  //protected:

    //calculate atomic gradient for single atom - cpu
    template <typename Dtype>
    float3 calc_atom_gradient_cpu(const float3& grid_origin, const Grid1f& coord, const Grid<Dtype, 3, false>& diff, float radius) const;

    //calculate gradient of type for type vector case
    template <typename Dtype>
    float calc_type_gradient_cpu(const float3& grid_origin, const Grid1f& coord, const Grid<Dtype, 3, false>& diff, float radius) const;


    //calculate atomic relevance for single atom - cpu
    template <typename Dtype>
    float calc_atom_relevance_cpu(const float3& grid_origin, const Grid1f& coord,  const Grid<Dtype, 3, false>& density, const Grid<Dtype, 3, false>& diff, float radius) const;

    /* \brief Find grid indices in one dimension that bound an atom's density.
     * @param[in] grid min coordinate in a given dimension
     * @param[in] atom coordinate in the same dimension
     * @param[in] atomic density radius (N.B. this is not the atomic radius)
     * @param[out] indices of grid points in the same dimension that could
     * possibly overlap atom density
     */
    CUDA_CALLABLE_MEMBER uint2 get_bounds_1d(const float grid_origin, float coord,
        float densityrad)  const;

    /* \brief Calculate atom density at a grid point.
     * @param[in] atomic coords
     * @param[in] atomic radius
     * @param[in] grid point coords
     * @param[out] atom density
     */
    template <bool Binary>
    CUDA_CALLABLE_MEMBER float calc_point(float ax, float ay, float az, float ar,
        const float3& grid_coords) const;

    //accumulate gradient from grid point x,y,z for provided atom at ax,ay,az
    CUDA_CALLABLE_MEMBER void accumulate_atom_gradient(float ax, float ay, float az,
            float x, float y, float z, float radius, float gridval, float3& agrad) const;

    template<typename Dtype> __global__ friend //member functions don't kernel launch
    void set_atom_gradients(GridMaker G, float3 grid_center, Grid2fCUDA coords, Grid1fCUDA type_index,
        Grid1fCUDA radii, Grid<Dtype, 4, true> grid, Grid<Dtype, 2, true> atom_gradients);
    template<typename Dtype> __global__ friend
    void set_atom_type_gradients(GridMaker G, float3 grid_origin, Grid2fCUDA coords, Grid2fCUDA type_vector,
        unsigned ntypes, Grid1fCUDA radii, Grid<Dtype, 4, true> grid, Grid<Dtype, 2, true> atom_gradients,
        Grid<Dtype, 2, true> type_gradients);
    template<typename Dtype> __global__ friend
    void set_atom_relevance(GridMaker G, float3 grid_origin, Grid2fCUDA coords, Grid1fCUDA type_index,
        Grid1fCUDA radii, Grid<Dtype, 4, true> densitygrid, Grid<Dtype, 4, true> diffgrid, Grid<Dtype, 1, true> relevance);
};

} /* namespace libmolgrid */

#endif /* GRID_MAKER_H_ */
