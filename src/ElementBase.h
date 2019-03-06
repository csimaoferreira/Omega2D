/*
 * ElementBase.h - abstract class for arrays of any computational elements
 *
 * (c)2018-9 Applied Scientific Research, Inc.
 *           Written by Mark J Stock <markjstock@gmail.com>
 */

#pragma once

#include "VectorHelper.h"
#include "Omega2D.h"
#include "Body.h"

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include <optional>
#include <variant>
#define _USE_MATH_DEFINES
#include <cmath>


// the superclass

template <class S>
class ElementBase {
public:
  ElementBase<S>(const size_t _n,
                 const elem_t _e,
                 const move_t _m,
                 std::shared_ptr<Body> _bp) :
      E(_e), M(_m), B(_bp), n(_n) {
  }

  size_t get_n() const { return n; }
  bool is_inert() const { return E==inert; }
  elem_t                                  get_elemt() const { return E; }
  move_t                                  get_movet() const { return M; }
  const std::shared_ptr<Body>             get_body_ptr() const { return B; }
  std::shared_ptr<Body>                   get_body_ptr()  { return B; }
  const std::array<Vector<S>,Dimensions>& get_pos() const { return x; }
  std::array<Vector<S>,Dimensions>&       get_pos()       { return x; }
  const Vector<S>&                        get_str() const { return *s; }
  Vector<S>&                              get_str()       { return *s; }
  const std::array<Vector<S>,Dimensions>& get_vel() const { return u; }
  std::array<Vector<S>,Dimensions>&       get_vel()       { return u; }

  void set_str(const size_t ioffset, const size_t icnt, Vector<S> _in) {
    assert(s);
    *s = _in;
/*
    if (s) {
      std::cout << "  in set_str, s exists" << std::endl;
      std::cout << "    icnt = " << icnt << std::endl;
      std::cout << "    _in.size() = " << _in.size() << std::endl;
      std::cout << "    (*s).size() = " << (*s).size() << std::endl;
      *s = _in;
    } else {
      std::cout << "  in set_str, s does not exist" << std::endl;
      std::cout << "    icnt = " << icnt << std::endl;
      std::cout << "    _in.size() = " << _in.size() << std::endl;
    }
    //s = std::move(_in);
*/
  }

  void add_new(std::vector<float>& _in) {

    // check inputs
    if (_in.size() == 0) return;
    const size_t nper = (this->E == inert) ? 2 : 4;
    assert(_in.size() % nper == 0);
    const size_t nnew = _in.size()/nper;

    // this initialization is specific to Points - so should we do it there?
    for (size_t d=0; d<Dimensions; ++d) {
      // extend with more space for new values
      x[d].resize(n+nnew);
      // copy new values to end of vector
      for (size_t i=0; i<nnew; ++i) {
        x[d][n+i] = _in[nper*i+d];
      }
    }

    // strength
    if (s) {
      // must dereference s to get the actual vector
      (*s).resize(n+nnew);
      for (size_t i=0; i<nnew; ++i) {
        (*s)[n+i] = _in[nper*i+2];
      }
    }

    // extend the other vectors as well
    for (size_t d=0; d<Dimensions; ++d) {
      u[d].resize(n+nnew);
    }
    //if (dsdt) {
    //  for (size_t d=0; d<Dimensions; ++d) {
    //    (*dsdt)[d].resize(n+nnew);
    //  }
    //}

    // finally, update n
    n += nnew;
  }

  // up-size all arrays to the new size, filling with sane values
  // this only happens right after diffusion
  void resize(const size_t _nnew) {
    const size_t currn = n;
    //std::cout << "  inside ElementBase::resize with " << currn << " " << _nnew << std::endl;
    if (_nnew == currn) return;

    // positions first
    for (size_t d=0; d<Dimensions; ++d) {
      const size_t thisn = x[d].size();
      x[d].resize(_nnew);
      for (size_t i=thisn; i<_nnew; ++i) {
        x[d][i] = 0.0;
      }
    }

    // strength
    if (s) {
      const size_t thisn = (*s).size();
      (*s).resize(_nnew);
      for (size_t i=thisn; i<_nnew; ++i) {
        (*s)[i] = 0.0;
      }
    }

    // and finally velocity (no need to set it)
    for (size_t d=0; d<Dimensions; ++d) {
      u[d].resize(_nnew);
    }

    // lastly, update n
    n = _nnew;
  }

  void zero_vels() {
    for (size_t d=0; d<Dimensions; ++d) {
      for (size_t i=0; i<get_n(); ++i) {
        u[d][i] = 0.0;
      }
    }
  }
  void finalize_vels(const std::array<double,Dimensions>& _fs) {
    const double factor = 0.5/M_PI;
    for (size_t d=0; d<Dimensions; ++d) {
      for (size_t i=0; i<get_n(); ++i) {
        u[d][i] = _fs[d] + u[d][i] * factor;
      }
    }
  }

  void add_body_motion(const S factor, const double _time) {
    // do nothing here
  }

  void add_rot_strengths(const S _factor) {
    // do nothing here
  }

  void transform(const double _time) {
    // reset positions according to prescribed motion
    if (B and M == bodybound) {
      // tell the Body to compute and save its position, vel, angular pos and angular vel
      B->transform(_time);

      // for the no-rotation case, we can just transform here
      std::array<double,Dimensions> thispos = B->get_pos();
      const double theta = B->get_orient();
      const S st = std::sin(M_PI * theta / 180.0);
      const S ct = std::cos(M_PI * theta / 180.0);

      std::cout << "    transforming body at time " << (S)_time << " to " << (S)thispos[0] << " " << (S)thispos[1] << " and theta " << theta << std::endl;

      // and do the transform
      for (size_t i=0; i<get_n(); ++i) {
        // rotate and translate
        x[0][i] = (S)thispos[0] + (*ux)[0][i]*ct - (*ux)[1][i]*st;
        x[1][i] = (S)thispos[1] + (*ux)[0][i]*st + (*ux)[1][i]*ct;
      }
    }
  }

  // time is the starting time, time_dt is the ending time
  void move(const double _time, const double _dt) {
    if (M == lagrangian) {
      std::cout << "  Moving" << to_string() << std::endl;

      // update positions
      for (size_t d=0; d<Dimensions; ++d) {
        for (size_t i=0; i<n; ++i) {
          x[d][i] += (S)_dt * u[d][i];
        }
      }

      // update strengths (in derived class)

    } else if (B and M == bodybound) {
      transform(_time+_dt);
    }
  }

  // time is the starting time, time_dt is the ending time
  void move(const double _time, const double _dt,
            const double _wt1, ElementBase<S> const & _u1,
            const double _wt2, ElementBase<S> const & _u2) {
    // must confirm that incoming time derivates include velocity
    // if this has vels, then lets advect it
    if (M == lagrangian) {
      std::cout << "  Moving" << to_string() << std::endl;

      // update positions
      for (size_t d=0; d<Dimensions; ++d) {
        for (size_t i=0; i<n; ++i) {
          x[d][i] += (S)_dt * (_wt1*_u1.u[d][i] + _wt2*_u2.u[d][i]);
        }
      }

      // update strengths (in derived class)

    } else if (B and M == bodybound) {
      transform(_time+_dt);
    }
  }

  // find the new peak strength magnitude
  S get_max_str() {
    if (s) {
      // we have strengths, go through and check them
      S thismax = 0.0;
      for (size_t i=0; i<(*s).size(); ++i) {
        S thisstr = std::abs((*s)[i]);
        if (thisstr > thismax) thismax = thisstr;
      }
      return thismax;
    } else {
      return 1.0;
    }
  }

  // add and return the total circulation of all elements
  S get_total_circ(const double _time) {
    if (s) {
      // we have strengths, add them up
      // this is the c++17 way
      //return std::reduce(std::execution::par, s->begin(), s->end());
      // this is the c++11 way
      return std::accumulate(s->begin(), s->end(), 0.0);
    } else {
      return 0.0;
    }
  }

  std::string to_string() const {
    std::string mystr = " " + std::to_string(n);
    if (E == active) {
      mystr += " Active";
    } else if (E == reactive) {
      mystr += " Reactive";
    } else {
      mystr += " Inert";
    }
    if (M == lagrangian) {
      mystr += " Lagrangian";
    } else if (M == bodybound) {
      mystr += " Body-fixed";
    } else {
      mystr += " Fixed";
    }
    return mystr;
  }

protected:
  // if you add anything here, you need to wipe out all build files and run cmake again!

  // active, reactive, or inert?
  elem_t E;
  // how does it move? use move_t and Body*
  move_t M;
  // if attached to a body, which one?
  std::shared_ptr<Body> B;

  // common arrays for all derived types
  size_t n;

  // state vector
  std::array<Vector<S>,Dimensions> x;                   // position
  std::optional<Vector<S>> s;                           // strength

  // time derivative of state vector
  std::array<Vector<S>,Dimensions> u;                   // velocity
  //std::optional<std::array<Vector<S>,Dimensions>> dsdt; // strength change

  // for objects moving with a body
  std::optional<std::array<Vector<S>,Dimensions>> ux;   // untransformed position
};

