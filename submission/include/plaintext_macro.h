// Copyright 2023-present Niobium Microsystems, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _PLAINTEXT_MACRO_H_
#define _PLAINTEXT_MACRO_H_

#include <iostream>
// helper macros to pause plaintext creation during recording time
#define MAKE_PLAINTEXT_HELPER(type_info, var_name, context, values) type_info var_name = context->MakeCKKSPackedPlaintext(values)


#ifdef NIOBIUM_COMPILER
#include "niobium/compiler.h"
#define MAKE_PLAINTEXT(type_info, var_name, context, values)\
  bool var_name ## _flag = niobium::compiler().running_p();\
  if(var_name ## _flag){\
    niobium::compiler().pause();\
  }\
  MAKE_PLAINTEXT_HELPER(type_info, var_name, context, values);\
  if(var_name ## _flag){\
    niobium::compiler().tag_input(#var_name, var_name);\
    niobium::compiler().resume(); \
  }
#else
#define MAKE_PLAINTEXT(type_info, var_name, context, values) MAKE_PLAINTEXT_HELPER(type_info, var_name, context, values)
#endif

#endif // _PLAINTEXT_MACRO_H_