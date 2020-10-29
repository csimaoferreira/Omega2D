/*
 * SolnType.h - Solution types from influence calculations
 *
 * (c)2020 Applied Scientific Research, Inc.
 *         Mark J Stock <markjstock@gmail.com>
 */

#pragma once

#include <string>

// solver type/order
enum solution_t {
  velonly    = 1,
  velandgrad = 2,
  psionly    = 3,
  velandvort = 4
};

//
// Class for the execution environment
//
class SolnType {
public:
  // primary constructor
  SolnType(const bool _psi,
           const bool _vel,
           const bool _grad,
           const bool _vort)
    : m_psi(_psi),
      m_vel(_vel),
      m_grad(_grad),
      m_vort(_vort)
    {}

  // default (delegating) ctor, solve for vels only
  SolnType()
    : SolnType(false, true, false, false)
    {}

  // take a descriptive enum
  SolnType(const solution_t _type)
    : SolnType(_type==psionly, _type!=psionly, _type==velandgrad, _type==velandvort)
    {}

  bool compute_psi() const { return m_psi; }
  bool compute_vel() const { return m_vel; }
  bool compute_grad() const { return m_grad; }
  bool compute_vort() const { return m_vort; }
  //const solution_t get_soln_type() const { return 

  std::string to_string() const {
    std::string mystr;
    bool first = true;
    if (m_psi) {
      if (first) {
        mystr += " for (";
        first = false;
      } else {
        mystr += ",";
      }
      mystr += " psi";
    }
    if (m_vel) {
      if (first) {
        mystr += " for (";
        first = false;
      } else {
        mystr += ",";
      }
      mystr += " vel";
    }
    if (m_grad) {
      if (first) {
        mystr += " for (";
        first = false;
      } else {
        mystr += ",";
      }
      mystr += " grads";
    }
    if (not first) mystr += ")";
    return mystr;
  }

protected:
  bool m_psi;	// streamfunction (1-component)
  bool m_vel;	// velocities (2-component)
  bool m_grad;	// velocity gradients (2x2 matrix)
  bool m_vort;	// vorticity (1-component)
};

