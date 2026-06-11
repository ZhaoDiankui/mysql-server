/*
   Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
   Portions Copyright (c) 2020, Alibaba Group Holding Limited
   Copyright (c) 2012,2013 Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
#ifndef SE_DICT_XE_FIELD_SKIP_FUNC_H_
#define SE_DICT_XE_FIELD_SKIP_FUNC_H_

#include "my_compiler.h"

class Field;

namespace smartengine
{
class SeFieldPacking;
class SeStringReader;

/*
  Function of type se_index_field_skip_t
*/

int se_skip_max_length(const SeFieldPacking *const fpi,
                       const Field *const field,
                       SeStringReader *const reader);

int se_skip_variable_length(const SeFieldPacking *const fpi,
                            const Field *const field,
                            SeStringReader *const reader);

/*
  Skip a keypart that uses Variable-Length Space-Padded encoding
*/
int se_skip_variable_space_pad(const SeFieldPacking *const fpi,
                               const Field *const field,
                               SeStringReader *const reader);

} //namespace smartengine
#endif
