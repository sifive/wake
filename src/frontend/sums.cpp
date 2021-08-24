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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <sstream>
#include <iostream>

#include "frontend/sums.h"
#include "frontend/diagnostic.h"
#include "types/datatype.h"

std::shared_ptr<Sum> Boolean;
std::shared_ptr<Sum> Order;
std::shared_ptr<Sum> List;
std::shared_ptr<Sum> Unit;
std::shared_ptr<Sum> Pair;
std::shared_ptr<Sum> Result;
std::shared_ptr<Sum> JValue;

void check_special(const std::shared_ptr<Sum> &sump) {
  if (sump->name == "Boolean") Boolean = sump;
  if (sump->name == "Order")   Order = sump;
  if (sump->name == "List")    List = sump;
  if (sump->name == "Unit")    Unit = sump;
  if (sump->name == "Pair")    Pair = sump;
  if (sump->name == "Result")  Result = sump;
  if (sump->name == "JValue")  JValue = sump;
}

bool sums_ok() {
  bool ok = true;

  if (Boolean) {
    if (Boolean->members.size() != 2 ||
        Boolean->members[0].ast.args.size() != 0 ||
        Boolean->members[1].ast.args.size() != 0) {
      std::ostringstream message;
      message << "Special constructor Boolean not defined correctly at "
        << Boolean->region.file() << ".";
      reporter->reportError(Boolean->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type Boolean@wake not defined." << std::endl;
    ok = false;
  }

  if (Order) {
    if (Order->members.size() != 3 ||
        Order->members[0].ast.args.size() != 0 ||
        Order->members[1].ast.args.size() != 0 ||
        Order->members[2].ast.args.size() != 0) {
      std::ostringstream message;
      message << "Special constructor Order not defined correctly at "
        << Order->region.file() << ".";
      reporter->reportError(Order->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type Order@wake not defined." << std::endl;
    ok = false;
  }

  if (List) {
    if (List->members.size() != 2 ||
        List->members[0].ast.args.size() != 0 ||
        List->members[1].ast.args.size() != 2) {
      std::ostringstream message;
      message << "Special constructor List not defined correctly at "
        << List->region.file() << ".";
      reporter->reportError(List->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type List@wake not defined." << std::endl;
    ok = false;
  }

  if (Unit) {
    if (Unit->members.size() != 1 ||
        Unit->members[0].ast.args.size() != 0) {
      std::ostringstream message;
      message << "Special constructor Unit not defined correctly at "
        << Unit->region.file() << ".";
      reporter->reportError(Unit->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type Unit@wake not defined." << std::endl;
    ok = false;
  }

  if (Pair) {
    if (Pair->members.size() != 1 ||
        Pair->members[0].ast.args.size() != 2) {
      std::ostringstream message;
      message << "Special constructor Pair not defined correctly at "
        << Pair->region.file() << ".";
      reporter->reportError(Pair->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type Pair@wake not defined." << std::endl;
    ok = false;
  }

  if (Result) {
    if (Result->members.size() != 2 ||
        Result->members[0].ast.args.size() != 1 ||
        Result->members[1].ast.args.size() != 1) {
      std::ostringstream message;
      message << "Special constructor Result not defined correctly at "
        << Result->region.file() << ".";
      reporter->reportError(Result->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type Result@wake not defined." << std::endl;
    ok = false;
  }

  if (JValue) {
    if (JValue->members.size() != 7 ||
        JValue->members[0].ast.args.size() != 1 ||
        JValue->members[1].ast.args.size() != 1 ||
        JValue->members[2].ast.args.size() != 1 ||
        JValue->members[3].ast.args.size() != 1 ||
        JValue->members[4].ast.args.size() != 0 ||
        JValue->members[5].ast.args.size() != 1 ||
        JValue->members[6].ast.args.size() != 1) {
      std::ostringstream message;
      message << "Special constructor JValue not defined correctly at "
        << JValue->region.file() << ".";
      reporter->reportError(JValue->region, message.str());
      ok = false;
    }
  } else {
    std::cerr << "Required data type JValue@wake not defined." << std::endl;
    ok = false;
  }

  return ok;
}

