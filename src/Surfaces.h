/*
 * Surfaces.h - Specialized class for surfaces in 2D
 *
 * (c)2019 Applied Scientific Research, Inc.
 *         Written by Mark J Stock <markjstock@gmail.com>
 */

#pragma once

#include "Omega2D.h"
#include "VectorHelper.h"
#include "ElementBase.h"

#ifdef USE_GL
#include "GlState.h"
#include "RenderParams.h"
#include "OglHelper.h"
#include "ShaderHelper.h"
#include "glad.h"
#endif

#include <iostream>
#include <vector>
#include <array>
#include <algorithm> // for max_element
#include <optional>
#include <cassert>

// use cpp until these become real-time variables
#define VORTEX_UNKNOWN
//#define SOURCE_UNKNOWN


// useful structure for panel strengths and BCs
// Strength<S> ps;
//   ps[0] means that the tangential (vortex) strength is present
//   ps[1] means that the normal (source) strength is present
template <class S> using Strength = std::array<std::optional<Vector<S>>,2>;

// useful structure for basis vectors
// Basis<S> b;
//   b[0] is the pair of arrays of the tangential normalized vectors, b[0][0] for x, b[0][1] for y
//   b[1] is the same for the normal vectors (+ is into fluid), b[1][0] for x, b[1][1] for y
template <class S> using Basis = std::array<std::array<Vector<S>,Dimensions>,Dimensions>;


// 1-D elements
template <class S>
class Surfaces: public ElementBase<S> {
public:
  // constructor - accepts vector of vectors of (x,y,s) pairs
  //               makes one boundary for each outer vector
  //               each inner vector must have even number of floats
  //               last parameter (_val) is either fixed strength or boundary
  //               condition for each panel
  Surfaces(const std::vector<S>&   _x,
           const std::vector<Int>& _idx,
           const std::vector<S>&   _val,
           const elem_t _e,
           const move_t _m,
           std::shared_ptr<Body> _bp)
    : ElementBase<S>(0, _e, _m, _bp),
      np(0),
      vol(-1.0),
      solved_omega(0.0),
      omega_error(0.0),
      this_omega(0.0),
      reabsorbed_gamma(0.0),
      max_strength(-1.0) {

    // make sure input arrays are correctly-sized
    assert(_idx.size() % 2 == 0 && "Index array is not an even multiple of 2");
    const size_t nsurfs = _idx.size() / 2;

    // if no surfs, quit out now
    if (nsurfs == 0) {
      // but still initialize ux before we go (in case first bfeature is not enabled)
      if (_bp) this->ux = this->x;
      // and init ps also
      if (this->E != inert) {
        Vector<S> new_s;
        ps = std::move(new_s);
      }
      return;
    }

    assert(_val.size() % nsurfs == 0 && "Value array is not an even multiple of panel count");
    assert(_x.size() % Dimensions == 0 && "Position array is not an even multiple of dimensions");
    const size_t nnodes = _x.size() / Dimensions;

    std::cout << "  new collection with " << nsurfs << " panels and " << nnodes << " nodes" << std::endl;

    // pull out the node locations, they go in the base class
    for (size_t d=0; d<Dimensions; ++d) {
      this->x[d].resize(nnodes);
      for (size_t i=0; i<nnodes; ++i) {
        this->x[d][i] = _x[Dimensions*i+d];
      }
    }

    // save them as untransformed if we are given a Body pointer
    if (_bp) {
      this->ux = this->x;
    }

    // copy over the node indices (with a possible type change)
    bool idx_are_all_good = true;
    idx.resize(_idx.size());
    for (size_t i=0; i<2*nsurfs; ++i) {
      // make sure it exists in the nodes array
      if (_idx[i] >= nnodes) idx_are_all_good = false;
      idx[i] = _idx[i];
    }
    assert(idx_are_all_good && "Some indicies are bad");

    // compute all basis vectors and panel areas
    compute_bases(nsurfs);

    // are strengths/values on a per-node or per-panel basis? - per panel now

    // now, depending on the element type, put the value somewhere
    if (this->E == active) {
      // value is a fixed strength for the segment
      Vector<S> new_s(_val.size());
      ps = std::move(new_s);
      std::copy(_val.begin(), _val.end(), ps->begin());

    } else if (this->E == reactive) {
      // value is a boundary condition
      #ifdef VORTEX_UNKNOWN
        Vector<S> new_bc(_val.size());
        bc[0] = std::move(new_bc);
        std::copy(_val.begin(), _val.end(), bc[0]->begin());
      #endif
      #ifdef SOURCE_UNKNOWN
        Vector<S> new_bc1(_val.size());
        bc[1] = std::move(new_bc1);
        std::copy(_val.begin(), _val.end(), bc[1]->begin());
      #endif

      // make space for the unknown, panel-centric strengths
      Vector<S> new_s(_val.size());
      ps = std::move(new_s);
      std::fill(ps->begin(), ps->end(), 0.0);

    } else if (this->E == inert) {
      // value is ignored (probably zero)
    }

    // velocity is per node, in the base class - just resize it here
    for (size_t d=0; d<Dimensions; ++d) {
      this->u[d].resize(nnodes);
    }

    // but panel velocity is per panel
    for (size_t d=0; d<Dimensions; ++d) {
      pu[d].resize(nsurfs);
    }

    // debug print
    if (false) {
      std::cout << "Nodes" << std::endl;
      for (size_t i=0; i<nnodes; ++i) {
        std::cout << "  " << i << " " << this->x[0][i] << " " << this->x[1][i] << std::endl;
      }
      std::cout << "Segments" << std::endl;
      for (size_t i=0; i<nsurfs; ++i) {
        std::cout << "  " << i << " " << idx[2*i] << " " << idx[2*i+1] << std::endl;
      }
    }

    // need to reset the base class n and the local np
    this->n = nnodes;
    np = nsurfs;

    // find geometric center
    if (this->M == bodybound) {
      set_geom_center();
    }
  }

  size_t                            get_npanels()     const { return np; }
  const S                           get_vol()         const { return vol; }
  const std::array<S,Dimensions>    get_geom_center() const { return tc; }

  // panel geometry
  const std::vector<Int>&                  get_idx()  const { return idx; }
  const std::array<Vector<S>,Dimensions>&  get_tang() const { return b[0]; }
  const std::array<Vector<S>,Dimensions>&  get_norm() const { return b[1]; }
  const Vector<S>&                         get_area() const { return area; }

  // override the ElementBase versions and send the panel-center vels
  const std::array<Vector<S>,Dimensions>&  get_vel()  const { return pu; }
  std::array<Vector<S>,Dimensions>&        get_vel()        { return pu; }

  // fixed or unknown surface strengths
  const Vector<S>&                         get_str()  const { return *ps; }
  Vector<S>&                               get_str()        { return *ps; }

  // strengths due to body rotation
  const bool                       have_rot_src_str() const { return (bool)rs[1]; }
  const Vector<S>&                 get_rot_vort_str() const { return *rs[0]; }
  const Vector<S>&                  get_rot_src_str() const { return *rs[1]; }

  // and (reactive only) boundary conditions
  const Vector<S>&                    get_tang_bcs()  const { return *bc[0]; }
  const Vector<S>&                    get_norm_bcs()  const { return *bc[1]; }

  // find out the next row index in the BEM after this collection
  void set_first_row(const Int _i) { istart = _i; }
  const Int num_unknowns_per_panel() const { return ((bool)bc[0] ? 1 : 0) + ((bool)bc[1] ? 1 : 0); }
  const Int get_first_row() const { return istart; }
  const Int get_num_rows()  const { return (get_npanels()*num_unknowns_per_panel() + (is_augmented() ? 1 : 0)); }
  const Int get_next_row()  const { return istart+get_num_rows(); }

  // assign the new strengths from BEM - do not let base class do this
  void set_str(const size_t ioffset, const size_t icnt, Vector<S> _in) {
    assert(ps && "Strength array does not exist");

    // pop off the "unknown" rotation rate and save it
    if (is_augmented()) {
      solved_omega = _in.back();
      std::cout << "    solved rotation rate is " << solved_omega << std::endl;
      omega_error = solved_omega - this->B->get_rotvel();
      std::cout << "    error in rotation rate is " << omega_error << std::endl;
      _in.pop_back();
    }

    assert(_in.size() == (*ps).size() && "Set strength array size does not match");
    //assert(ioffset == 0 && "Offset is not zero");

    // copy over the strengths
    *ps = _in;
  }

  // a little logic to see if we should augment the BEM equations for this object
  const bool is_augmented() const {
    bool augment = true;

    // don't augment the ground body, or the boundary to an internal flow
    if (this->B) {
      // is the body pointer ground?
      if (std::string("ground").compare(this->B->get_name()) == 0) {
        // now, does the object bound internal flow?
        if (vol < 0.0) augment = false;
      }
    } else {
      // nullptr for Body? no augment (old way of turning it off)
      augment = false;
    }
    // and only need to augment reactive surfaces (ones participating in BEM)
    if (this->E != reactive) augment = false;

    // force no augmentation at all
    //augment = false;

    return augment;
  }

  const float get_max_bc_value() const {
    if (this->E == reactive) {
      S this_max = 0.0;
      #ifdef VORTEX_UNKNOWN
        const S vort_max = *std::max_element(std::begin(*bc[0]), std::end(*bc[0]));
        const S vort_min = *std::min_element(std::begin(*bc[0]), std::end(*bc[0]));
        this_max = std::max(this_max, std::max(vort_max, -vort_min));
      #endif
      #ifdef SOURCE_UNKNOWN
        const S src_max = *std::max_element(std::begin(*bc[1]), std::end(*bc[1]));
        const S src_min = *std::min_element(std::begin(*bc[1]), std::end(*bc[1]));
        this_max = std::max(this_max, std::max(src_max, -src_min));
      #endif
      return (float)this_max;
    } else {
      return (float)0.0;
    }
  }

  // add more nodes and panels to this collection
  void add_new(const std::vector<S>&   _x,
               const std::vector<Int>& _idx,
               const std::vector<S>&   _val) {

    // remember old sizes of nodes and element arrays
    const size_t nnold = this->n;
    const size_t neold = get_npanels();

    // make sure input arrays are correctly-sized
    assert(_idx.size() % Dimensions == 0 && "Index array is not an even multiple of dimensions");
    const size_t nsurfs = _idx.size() / Dimensions;
    // if no surfs, quit out now
    if (nsurfs == 0) return;

    assert(_val.size() % nsurfs == 0 && "Value array is not an even multiple of panel count");
    assert(_x.size() % Dimensions == 0 && "Position array is not an even multiple of dimensions");
    const size_t nnodes = _x.size() / Dimensions;

    std::cout << "  adding " << nsurfs << " new surface panels and " << nnodes << " new points to collection..." << std::endl;

    // DON'T call the method in the base class, because we do things differently here
    //ElementBase<S>::add_new(_in);

    // pull out the node locations, they are base class
    for (size_t d=0; d<Dimensions; ++d) {
      this->x[d].resize(nnold+nnodes);
      for (size_t i=0; i<nnodes; ++i) {
        this->x[d][nnold+i] = _x[Dimensions*i+d];
      }
    }

    // save them as untransformed if we have a Body pointer
    if (this->B) {
      for (size_t d=0; d<Dimensions; ++d) {
        (*this->ux)[d].resize(nnold+nnodes);
        for (size_t i=nnold; i<nnold+nnodes; ++i) {
          (*this->ux)[d][i] = this->x[d][i];
        }
      }
    }

    // copy over the node indices, taking care to offset into the new array
    bool idx_are_all_good = true;
    idx.resize(2*neold + _idx.size());
    for (size_t i=0; i<2*nsurfs; ++i) {
      // make sure it exists in the nodes array
      if (_idx[i] >= nnold+nnodes) idx_are_all_good = false;
      idx[2*neold+i] = nnold + _idx[i];
    }
    assert(idx_are_all_good && "Some indicies are bad");

    // compute all basis vectors and panel areas
    compute_bases(neold+nsurfs);

    // now, depending on the element type, put the value somewhere - but panel-wise, so here
    if (this->E == active) {
      // value is a fixed strength for the element
      ps->resize(neold+nsurfs); 

    } else if (this->E == reactive) {
      // value is a boundary condition
      #ifdef VORTEX_UNKNOWN
        bc[0]->reserve(neold+nsurfs); 
        bc[0]->insert(bc[0]->end(), _val.begin(), _val.end());
      #endif
      #ifdef SOURCE_UNKNOWN
        bc[1]->reserve(neold+nsurfs); 
        bc[1]->insert(bc[1]->end(), _val.begin(), _val.end());
      #endif
      // and we still need strengths
      ps->resize(neold+nsurfs); 

    } else if (this->E == inert) {
      // value is ignored (probably zero)
    }

    // velocity is in the base class - just resize it here
    for (size_t d=0; d<Dimensions; ++d) {
      this->u[d].resize(nnold+nnodes);
    }

    // panel velocity is here
    for (size_t d=0; d<Dimensions; ++d) {
      pu[d].resize(neold+nsurfs);
    }

    // debug print
    if (false) {
      std::cout << "Nodes" << std::endl;
      for (size_t i=0; i<nnold+nnodes; ++i) {
        std::cout << "  " << i << " " << this->x[0][i] << " " << this->x[1][i] << std::endl;
      }
      std::cout << "Segments" << std::endl;
      for (size_t i=0; i<neold+nsurfs; ++i) {
        std::cout << "  " << i << " " << idx[2*i] << " " << idx[2*i+1] << std::endl;
      }
    }

    // need to reset the base class n
    this->n += nnodes;
    np += nsurfs;

    // re-find geometric center
    if (this->M == bodybound) {
      set_geom_center();
    }
  }

  void add_body_motion(const S _factor, const double _time) {
    // no need to call base class now
    //ElementBase<S>::add_body_motion(_factor);

    // if no body pointer, or attached to ground, return
    if (not this->B) return;
    if (std::string("ground").compare(this->B->get_name()) == 0) return;

    // make sure we've calculated transformed center (we do this when we do volume)
    assert(vol > 0.0 && "Have not calculated transformed center, or volume is negative");
    // and we trust that we've transformed utc to tc

    // do this for all nodes - what about panels?
    for (size_t i=0; i<get_npanels(); ++i) {

      // apply the translational velocity
      std::array<double,Dimensions> thisvel = this->B->get_vel(_time);
      for (size_t d=0; d<Dimensions; ++d) {
        pu[d][i] += _factor * (float)thisvel[d];
      }

      // now compute the rotational velocity with respect to the geometric center
      double thisrotvel = this->B->get_rotvel(_time);
      // center of this panel
      Int id0 = idx[2*i];
      Int id1 = idx[2*i+1];
      // panel center
      const S xc = 0.5 * (this->x[0][id1] + this->x[0][id0]);
      const S yc = 0.5 * (this->x[1][id1] + this->x[1][id0]);
      // add rotational velocity
      pu[0][i] -= _factor * (float)thisrotvel * (yc - tc[1]);
      pu[1][i] += _factor * (float)thisrotvel * (xc - tc[0]);
    }
  }
 
  void zero_strengths() {
    // call base class first
    ElementBase<S>::zero_strengths();

    // and reset any panel strengths
    if (ps) {
      std::fill(ps->begin(), ps->end(), 0.0);
    }

    // and reset the rotational strengths here
    if (rs[0]) std::fill(rs[0]->begin(), rs[0]->end(), 0.0);
    if (rs[1]) std::fill(rs[1]->begin(), rs[1]->end(), 0.0);
  }

  // three ways to add source and vortex rotational strengths to the surface
  // first: as a multiple of the current, defined rotation rate
  void add_rot_strengths(const S _factor) {
    // if no parent Body, forget it
    if (not this->B) return;
    // get current rotation rate
    const S rotvel = (S)this->B->get_rotvel();
    // call parent
    add_rot_strengths_base(_factor * rotvel);
  }

  // second: assuming unit rotation rate (for finding the BEM influence matrix)
  void add_unit_rot_strengths() {
    add_rot_strengths_base(1.0);
  }

  // third: as a multiple of the rotation rate
  void add_solved_rot_strengths(const S _factor) {
    if (is_augmented()) {
      // use the augmented-BEM result for rotation rate
      add_rot_strengths_base(_factor * solved_omega);
    } else {
      // use the predefined rotationrate
      add_rot_strengths(_factor);
    }
  }

  // augment the strengths with a value equal to that which accounts for
  //   the solid-body rotation of the object
  // NOTE: this needs to provide both the vortex AND source strengths!
  // AND we don't have the time - assume bodies have been transformed
  void add_rot_strengths_base(const S _factor) {

    // if no rotation, strengths, or no parent Body, or attached to ground, then no problem!
    if (not this->B) return;
    if (not ps) return;
    if (std::string("ground").compare(this->B->get_name()) == 0) return;

    //if (std::abs(_factor) < std::numeric_limits<float>::epsilon()) return;

    // make sure we've calculated transformed center (we do this when we do volume)
    assert(vol > 0.0 && "Have not calculated transformed center, or volume is negative");
    // and we trust that we've transformed utc to tc

    // have we made rs yet? or is it the right size?
    for (size_t i=0; i<2; i++) {
      if (rs[i]) {
        rs[i]->resize(get_npanels());
      } else {
        // value is a fixed strength for the segment
        Vector<S> new_rs(get_npanels());
        rs[i] = std::move(new_rs);
      }
    }

    //std::cout << "Inside add_rot_strengths, sizes are: " << get_npanels() << " " << this->s->size() << " " << rs[0]->size() << std::endl;
    assert(ps->size() == get_npanels() && "Strength array is not the same as panel count");

    // still here? let's do it. use the untransformed coordinates
    for (size_t i=0; i<get_npanels(); i++) {
      const size_t j   = idx[2*i];
      const size_t jp1 = idx[2*i+1];
      // vector from object geometric center to panel center
      const S dx = 0.5 * ((*this->ux)[0][j] + (*this->ux)[0][jp1]) - utc[0];
      const S dy = 0.5 * ((*this->ux)[1][j] + (*this->ux)[1][jp1]) - utc[1];
      // velocity of the panel center
      const S ui = -_factor * dy;
      const S vi =  _factor * dx;

      // panel tangential vector, fluid to the left, body to the right
      S panelx = (*this->ux)[0][jp1] - (*this->ux)[0][j];
      S panely = (*this->ux)[1][jp1] - (*this->ux)[1][j];
      const S oopanell = 1.0 / area[i];
      panelx *= oopanell;
      panely *= oopanell;

      // the vortex strength - we ADD to the existing
      const S new_vort = -1.0 * (ui*panelx + vi*panely);
      (*ps)[i] += new_vort;
      (*rs[0])[i] += new_vort;

      // the source strength
      const S new_src = -1.0 * (ui*panely - vi*panelx);
      (*rs[1])[i] += new_src;

      // debug print
      if (_factor > 0.0 and false) {
        std::cout << "  panel " << i << " at " << dx << " " << dy << " adds to vortex str "
                  << new_vort << " and source str " << new_src << std::endl;
      }
    }
  }

  // calculate the geometric center of all geometry in this object
  void set_geom_center() {

    // we must have an attached body and a set of untransformed coordinates
    assert(this->B && "Body pointer has not been set");
    assert(this->ux && "Untransformed positions have not been set");

    std::cout << "  inside Surfaces::set_geom_center with " << get_npanels() << " panels" << std::endl;

    // iterate over panels, accumulating area and CM
    S asum = 0.0;
    S xsum = 0.0;
    S ysum = 0.0;
    for (size_t i=0; i<get_npanels(); i++) {
      const size_t j   = idx[2*i];
      const size_t jp1 = idx[2*i+1];
      // assume a triangle from 0,0 to two ends of each panel
      const S xc = (0.0 + (*this->ux)[0][j] + (*this->ux)[0][jp1]) / 3.0;
      const S yc = (0.0 + (*this->ux)[1][j] + (*this->ux)[1][jp1]) / 3.0;
      const S panelx = (*this->ux)[0][jp1] - (*this->ux)[0][j];
      const S panely = (*this->ux)[1][jp1] - (*this->ux)[1][j];
      // and the side lengths
      const S a = std::sqrt(std::pow((*this->ux)[0][j],2)+std::pow((*this->ux)[1][j],2));
      const S b = std::sqrt(std::pow(panelx,2)+std::pow(panely,2));
      const S c = std::sqrt(std::pow((*this->ux)[0][jp1],2)+std::pow((*this->ux)[1][jp1],2));
      //std::cout << "  panel " << i << " has side lens " << a << " " << b << " " << c << std::endl;
      // Heron's formula for the area
      const S hs = 0.5*(a+b+c);
      S thisarea = std::sqrt(hs*(hs-a)*(hs-b)*(hs-c));
      // negate area if the winding is backwards
      if ((*this->ux)[1][j]*panelx - (*this->ux)[0][j]*panely < 0.0) thisarea = -thisarea;
      // add this to the running sums
      //std::cout << "    and area " << thisarea << " and center " << xc << " " << yc << std::endl;
      asum += thisarea;
      xsum += xc*thisarea;
      ysum += yc*thisarea;
    }
    vol = asum;
    utc[0] = xsum/vol;
    utc[1] = ysum/vol;

    std::cout << "    geom center is " << utc[0] << " " << utc[1] << " and area is " << vol << std::endl;
  }

  // need to maintain the 2x2 set of basis vectors for each panel
  // this also calculates the triangle areas
  // always recalculate everything!
  void compute_bases(const Int nnew) {

    assert(2*nnew == idx.size() && "Array size mismatch");

    // resize any vectors
    for (size_t i=0; i<Dimensions; ++i) {
      for (size_t j=0; j<Dimensions; ++j) {
        b[i][j].resize(nnew);
      }
    }
    area.resize(nnew);

    // we'll reuse these vectors
    std::array<S,Dimensions> x1, norm;

    // update what we need
    for (size_t i=0; i<nnew; ++i) {
      const size_t id0 = idx[2*i];
      const size_t id1 = idx[2*i+1];
      //std::cout << "elem near " << this->x[0][id0] << " " << this->x[1][id0] << std::endl;

      // x1 vector is along direction from node 0 to node 1
      for (size_t j=0; j<Dimensions; ++j) x1[j] = this->x[j][id1] - this->x[j][id0];
      const S base = std::sqrt(x1[0]*x1[0] + x1[1]*x1[1]);
      for (size_t j=0; j<Dimensions; ++j) x1[j] *= (1.0/base);
      //std::cout << "  has x1 " << x1[0] << " " << x1[1] << std::endl;

      // now we have the area
      area[i] = base;

      // the normal vector points into the fluid (to the left when walking from nodes 0 to 1)
      norm[0] = -x1[1];
      norm[1] =  x1[0];
      //std::cout << "  norm " << norm[0] << " " << norm[1] << std::endl;

      // and assign
      for (size_t j=0; j<Dimensions; ++j) b[0][j][i] = x1[j];
      for (size_t j=0; j<Dimensions; ++j) b[1][j][i] = norm[j];

      //std::cout << "elem near " << this->x[0][id0] << " " << this->x[1][id0] << " has norm " << b[2][0][i] << " " << b[2][1][i] << " " << b[2][2][i] << std::endl;
    }
  }

  // when transforming a body-bound object to a new time, we must also transform the geometric center
  void transform(const double _time) {
    // must explicitly call the method in the base class
    ElementBase<S>::transform(_time);

    // and recalculate the basis vectors
    compute_bases(np);

    if (this->B and this->M == bodybound) {
    //if (this->B) {
      // prepare for the transform
      std::array<double,Dimensions> thispos = this->B->get_pos();
      const double theta = this->B->get_orient();
      const S st = std::sin(theta);
      const S ct = std::cos(theta);

      // transform the utc to tc here
      tc[0] = (S)thispos[0] + utc[0]*ct - utc[1]*st;
      tc[1] = (S)thispos[1] + utc[0]*st + utc[1]*ct;

    } else {
      // transform the utc to tc here
      tc[0] = utc[0];
      tc[1] = utc[1];
    }
  }


  void zero_vels() {
    // zero the local, panel-center vels
    for (size_t d=0; d<Dimensions; ++d) {
      std::fill(pu[d].begin(), pu[d].end(), 0.0);
    }
    // then explicitly call the method in the base class to zero theirs
    ElementBase<S>::zero_vels();
  }

  void finalize_vels(const std::array<double,Dimensions>& _fs) {
    // finalize panel-center vels first
    const double factor = 0.5/M_PI;
    for (size_t d=0; d<Dimensions; ++d) {
      for (size_t i=0; i<get_npanels(); ++i) {
        pu[d][i] = _fs[d] + pu[d][i] * factor;
      }
    }
    // must explicitly call the method in the base class, too
    ElementBase<S>::finalize_vels(_fs);
  }

/*
  // up-size all arrays to the new size, filling with sane values
  void resize(const size_t _nnew) {
    const size_t currn = this->n;
    //std::cout << "  inside Surfaces::resize with " << currn << " " << _nnew << std::endl;

    // must explicitly call the method in the base class - this sets n
    ElementBase<S>::resize(_nnew);

    if (_nnew == currn) return;

    // radii here
    const size_t thisn = r.size();
    r.resize(_nnew);
    for (size_t i=thisn; i<_nnew; ++i) {
      r[i] = 1.0;
    }
  }

  //
  // 1st order Euler advection and stretch
  //
  void move(const double _time, const double _dt) {
    // must explicitly call the method in the base class
    ElementBase<S>::move(_time, _dt);

    // no specialization needed
    if (this->M == lagrangian and this->E != inert) {
      //std::cout << "  Stretching" << to_string() << " using 1st order" << std::endl;
      S thismax = 0.0;

      for (size_t i=0; i<this->n; ++i) {
        S this_s = (*this->s)[i];

        // compute stretch term
        std::array<S,2> wdu = {0.0};

        // add Cottet SFS

        // update strengths
        (*this->s)[i] = this_s + _dt * wdu[0];

        // check for max strength
        S thisstr = std::abs((*this->s)[i]);
        if (thisstr > thismax) thismax = thisstr;

      }
      if (max_strength < 0.0) {
        max_strength = thismax;
      } else {
        max_strength = 0.1*thismax + 0.9*max_strength;
      }
      //std::cout << "  New max_strength is " << max_strength << std::endl;
    } else {
      //std::cout << "  Not stretching" << to_string() << std::endl;
      max_strength = 1.0;
    }
  }
*/

  //
  // return a particle version of the panels (useful during Diffusion)
  // offset is scaled by vdelta
  //
  std::vector<S> represent_as_particles(const S _offset, const S _vdelta) {

    // how many panels?
    const size_t num_pts = get_npanels();

    // init the output vector (x, y, s, r)
    std::vector<S> px(num_pts*4);

    // the fluid is to the left walking from one point to the next
    // so go CW around an external boundary starting at theta=0 (+x axis)

    for (size_t i=0; i<num_pts; i++) {
      Int id0 = idx[2*i];
      Int id1 = idx[2*i+1];
      // start at center of panel
      px[4*i+0] = 0.5 * (this->x[0][id1] + this->x[0][id0]);
      px[4*i+1] = 0.5 * (this->x[1][id1] + this->x[1][id0]);
      // push out a fixed distance (b[1] is the array of normal vectors)
      // this assumes properly resolved, vdelta and dt
      px[4*i+0] += _offset * b[1][0][i];
      px[4*i+1] += _offset * b[1][1][i];
      // the panel strength is the solved strength plus the boundary condition
      float this_str = (*ps)[i];
      // add on the (vortex) bc value here
      #ifdef VORTEX_UNKNOWN
        if (this->E == reactive) this_str += (*bc[0])[i];
      #endif
      // complete the element with a strength and radius
      px[4*i+2] = this_str * area[i];
      px[4*i+3] = _vdelta;
      //std::cout << "  new part is " << px[4*i+0] << " " << px[4*i+1] << " " << px[4*i+2] << " " << px[4*i+3] << std::endl;
    }


    return px;
  }

  // find the new peak strength magnitude
  S get_max_str() {
    if (ps) {
      const S this_max = *std::max_element(std::begin(*ps), std::end(*ps));
      const S this_min = *std::min_element(std::begin(*ps), std::end(*ps));
      return std::max(this_max, -this_min);
    } else {
      return 1.0;
    }
  }

  // smooth the peak strength magnitude
  void update_max_str() {
    S thismax = get_max_str();

    // and slowly update the current saved value
    if (max_strength < 0.0) {
      max_strength = thismax;
    } else {
      max_strength = 0.1*thismax + 0.9*max_strength;
    }
  }

  // add and return the total circulation of all elements
  //   specializing the one in ElementBase because we need
  //   to scale by panel length here
  S get_total_circ(const double _time) {
    S circ = 0.0;

    if (ps) {
      // we have strengths, add them up
      for (size_t i=0; i<get_npanels(); i++) {
        // total circulation is just vortex sheet strength times panel length
        circ += (*ps)[i] * area[i];
      }
    }

    return circ;
  }

  // add and return the total circulation of all elements
  S get_body_circ(const double _time) {
    S circ = 0.0;

    // do not call the parent
    if (this->B) {
      // we're attached to a body - great! what's the rotation rate?
      circ = 2.0 * vol * (S)this->B->get_rotvel(_time);
    } else {
      // we are fixed, thus not rotating
    }

    return circ;
  }

  // return the rotational speed from the previous step
  S get_last_body_circ() { return 2.0 * vol * (S)this_omega; }

  // compute the change in circulation of the rotating body since the last shedding event
/*
  S get_body_circ_change(const double _time) {
    const S old_circ = 2.0 * vol * (S)this_omega;
    S new_circ = 0.0;

    // do not call the parent
    if (this->B) {
      // we're attached to a body - great! what's the rotation rate?
      new_circ = 2.0 * vol * (S)this->B->get_rotvel(_time);
    } else {
      // we are fixed, thus not rotating
    }

    return (new_circ - old_circ);
  }
*/

  // add and return the total impulse of all elements
  std::array<S,Dimensions> get_total_impulse() {

    // here is the return vector
    std::array<S,Dimensions> imp;
    imp.fill(0.0);

    if (ps) {
      // make this easy - represent as particles - do we count BCs here?!?
      std::vector<S> pts = represent_as_particles(0.0, 1.0);

      // now compute impulse of those
      for (size_t i=0; i<get_npanels(); ++i) {
        const size_t idx = 4*i;
        imp[0] -= pts[idx+2] * pts[idx+1];
        imp[1] += pts[idx+2] * pts[idx+0];
      }
    }

    return imp;
  }

  // reset the circulation counter and saved rotation rate - useful for augmented BEM
  void reset_augmentation_vars() {
    this_omega = this->B->get_rotvel();
    reabsorbed_gamma = 0.0;
  }

  S get_last_body_circ_error() {
    return (S)(2.0 * vol * omega_error);
  }

  // *add* the given circulation to the reabsorbed accumulator
  void add_to_reabsorbed(const S _circ) {
    reabsorbed_gamma += _circ;
  }

  // return that amount of reabsorbed circulation
  S get_reabsorbed() {
    return reabsorbed_gamma;
  }


#ifdef USE_GL
  //
  // OpenGL functions
  //

  // helper function to clean up initGL
  void prepare_opengl_buffer(GLuint _prog, GLuint _idx, const GLchar* _name) {
    glBindBuffer(GL_ARRAY_BUFFER, mgl->vbo[_idx]);
    const GLint position_attribute = glGetAttribLocation(_prog, _name);
    // Specify how the data for position can be accessed
    glVertexAttribPointer(position_attribute, 1, get_gl_type<S>, GL_FALSE, 0, 0);
    // Enable the attribute
    glEnableVertexAttribArray(position_attribute);
  }

  // this gets done once - load the shaders, set up the vao
  void initGL(std::vector<float>& _projmat,
              float*              _poscolor,
              float*              _negcolor,
              float*              _defcolor) {

    //std::cout << "inside Surfaces.initGL" << std::endl;
    std::cout << "inside Surfaces.initGL with E=" << this->E << " and M=" << this->M << std::endl;

    // generate the opengl state object with space for 4 vbos and 1 shader program
    mgl = std::make_shared<GlState>(4,1);

    // Allocate space, but don't upload the data from CPU to GPU yet
    for (size_t i=0; i<Dimensions; ++i) {
      glBindBuffer(GL_ARRAY_BUFFER, mgl->vbo[i]);
      glBufferData(GL_ARRAY_BUFFER, 0, this->x[i].data(), GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mgl->vbo[Dimensions]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, idx.data(), GL_STATIC_DRAW);

    if (this->s) {
      glBindBuffer(GL_ARRAY_BUFFER, mgl->vbo[3]);
      glBufferData(GL_ARRAY_BUFFER, 0, (*ps).data(), GL_STATIC_DRAW);
    }

    // Load and create the blob-drawing shader program
    mgl->spo[0] = create_draw_surface_line_prog();

    // Now do the four arrays
    prepare_opengl_buffer(mgl->spo[0], 0, "px");
    prepare_opengl_buffer(mgl->spo[0], 1, "py");
    prepare_opengl_buffer(mgl->spo[0], 2, "rawstr");

    // and for the compute shaders! (later)

    // Get the location of the attributes that enters in the vertex shader
    mgl->projmat_attribute = glGetUniformLocation(mgl->spo[0], "Projection");

    // upload the projection matrix
    glUniformMatrix4fv(mgl->projmat_attribute, 1, GL_FALSE, _projmat.data());

    // locate where the colors and color scales go
    mgl->pos_color_attribute = glGetUniformLocation(mgl->spo[0], "pos_color");
    mgl->neg_color_attribute = glGetUniformLocation(mgl->spo[0], "neg_color");
    mgl->def_color_attribute = glGetUniformLocation(mgl->spo[0], "def_color");
    mgl->str_scale_attribute = glGetUniformLocation(mgl->spo[0], "str_scale");

    // send the current values
    glUniform4fv(mgl->pos_color_attribute, 1, (const GLfloat *)_poscolor);
    glUniform4fv(mgl->neg_color_attribute, 1, (const GLfloat *)_negcolor);
    glUniform4fv(mgl->def_color_attribute, 1, (const GLfloat *)_defcolor);
    glUniform1f (mgl->str_scale_attribute, (const GLfloat)1.0);
    //std::cout << "init pos color as " << _poscolor[0] << " " << _poscolor[1] << " " << _poscolor[2] << " " << _poscolor[3] << std::endl;

    // and indicate the fragment color output
    glBindFragDataLocation(mgl->spo[0], 0, "frag_color");

    glBindVertexArray(0);
  }

  // this gets done every time we change the size of the index array
  void updateGL() {
    //std::cout << "inside Surfaces.updateGL" << std::endl;

    // has this been init'd yet?
    if (not mgl) return;
    if (glIsVertexArray(mgl->vao) == GL_FALSE) return;

    const size_t vlen = this->x[0].size()*sizeof(S);
    if (vlen > 0) {
      glBindVertexArray(mgl->vao);

      // Indicate and upload the data from CPU to GPU
      for (size_t i=0; i<Dimensions; ++i) {
        // the positions
        glBindBuffer(GL_ARRAY_BUFFER, mgl->vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, vlen, this->x[i].data(), GL_DYNAMIC_DRAW);
      }

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mgl->vbo[Dimensions]);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Int)*idx.size(), idx.data(), GL_DYNAMIC_DRAW);

      // here is where we split on element type: active/reactive vs. inert
      if (this->E == inert) {
        // just don't upload strengths

      } else { // this->E is active or reactive
        // the strengths
        if (ps) {
          const size_t slen = ps->size()*sizeof(S);
          glBindBuffer(GL_ARRAY_BUFFER, mgl->vbo[3]);
          glBufferData(GL_ARRAY_BUFFER, slen, (*ps).data(), GL_DYNAMIC_DRAW);
        }
      }

      glBindVertexArray(0);

      // must tell draw call how many elements are there - or, really, how many indices
      mgl->num_uploaded = idx.size();
    }
  }

  // OpenGL3 stuff to draw segments, called once per frame
  void drawGL(std::vector<float>& _projmat,
              RenderParams&       _rparams,
              const float         _vdelta) {

    //std::cout << "inside Surfaces.drawGL" << std::endl;

    // has this been init'd yet?
    if (not mgl) {
      initGL(_projmat, _rparams.pos_circ_color,
                       _rparams.neg_circ_color,
                       _rparams.default_color);
      updateGL();
    }

    if (mgl->num_uploaded > 0) {
      glBindVertexArray(mgl->vao);

      // get blending ready
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);

      glLineWidth(2.0);

      // here is where we split on element type: active/reactive vs. inert
      //if (this->E == inert) {
      //} else { // this->E is active or reactive

      // draw as lines
      glUseProgram(mgl->spo[0]);

      // upload the current projection matrix
      glUniformMatrix4fv(mgl->projmat_attribute, 1, GL_FALSE, _projmat.data());

      // upload the current color values
      glUniform4fv(mgl->pos_color_attribute, 1, (const GLfloat *)_rparams.pos_circ_color);
      glUniform4fv(mgl->neg_color_attribute, 1, (const GLfloat *)_rparams.neg_circ_color);
      glUniform4fv(mgl->def_color_attribute, 1, (const GLfloat *)_rparams.default_color);
      glUniform1f (mgl->str_scale_attribute, (const GLfloat)max_strength);

      // the one draw call here
      glDrawElements(GL_LINES, mgl->num_uploaded, get_gl_type<Int>, 0);

      // return state
      glEnable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
      glBindVertexArray(0);
    }
  }
#endif

  std::string to_string() const {
    std::string retstr = " " + std::to_string(get_npanels()) + ElementBase<S>::to_string() + " Panels";
    return retstr;
  }

protected:
  // ElementBase.h has x, s, u, ux on the *nodes*


  size_t np;				// number of panels

  // element-wise variables special to triangular panels
  std::vector<Int>                 idx;	// indexes into the x array
  Vector<S>                       area; // panel areas
  Basis<S>                           b; // transformed basis vecs: tangent is b[0], normal is b[1], x norm is b[1][0]
  std::array<Vector<S>,Dimensions>  pu; // velocities on panel centers - this needs to NOT be called "u"

  // strengths and BCs
  std::optional<Vector<S>>          ps; // panel-wise strengths per unit length (for "active" and "reactive")
  Strength<S>                       bc; // boundary condition for the elements (only when "reactive")
  Strength<S>                       rs; // the strengths per unit length which represent the vel
                                        //   influence of the volume vorticity of the parent body
                                        //   (also used only when type is "reactive")

  // parameters for the encompassing body
  Int                           istart; // index of first entry in RHS vector and A matrix
  S                                vol; // volume of the body - for augmented BEM solution
  std::array<S,Dimensions>         utc; // untransformed geometric center
  std::array<S,Dimensions>          tc; // transformed geometric center

  // augmented-BEM-related
  double                  solved_omega; // rotation rate returned from augmented row in BEM
  double                   omega_error; // error in rotation rate from last BEM
  double                    this_omega; // rotation rate at most recent Diffusion step
  S                   reabsorbed_gamma; // amount of circulation reabsorbed by this collection since last Diffusion step

private:
#ifdef USE_GL
  std::shared_ptr<GlState> mgl;
#endif
  float max_strength;
};

