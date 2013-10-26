// -*-coding: mule-utf-8-unix; fill-column: 59 -*-
/** 
 * @file
 *
 * This file is a part of Bare C++ library. Bare C++
 * library is a standard library specially designed for
 * systems without memory protection like exokernel
 * operating systems.
 *
 * @author Sergei Lodyagin <serg@kogorta.dp.ua>
 * @copyright (C) 2013 Cohors LLC
 *
 */

#ifndef _BITS_EXT_CONSTR_H
#define _BITS_EXT_CONSTR_H

namespace std {
namespace bits {

//! T is a type to be constructed externally by a placement
//! new operator
template<class T>
union _externally_constructed
{
  _externally_constructed() {} // it is
  ~_externally_constructed() {} // never
  T m;
};

}
}


#endif
