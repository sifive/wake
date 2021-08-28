/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SFINAE_H
#define SFINAE_H

#define TEST_MEMBER(member)						\
  template <typename T>							\
  struct has_##member {							\
    typedef char yes;							\
    struct no { char x[2]; };						\
    template <typename C> static yes test(decltype(&C::member));	\
    template <typename C> static no  test(...); 			\
    static bool const value = sizeof(test<T>(0)) == sizeof(yes);	\
  }

template <bool C, typename T>
struct enable_if {
  typedef T type;
};

template <typename T>
struct enable_if<false, T> { };

#endif
