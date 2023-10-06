# Copyright 2023 SiFive, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You should have received a copy of LICENSE.Apache2 along with
# this software. If not, you may obtain a copy at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import subprocess

def prim_to_expr(prim_name: str, param_count: int) -> str:
    expr = '('
    for _ in range(param_count):
        expr += '\\_ '
    expr += 'prim "'
    expr += prim_name
    expr += '")'
    return expr

def determine_expr_type(expr: str) -> str:
    result = subprocess.run(['./bin/wake', '-q', '--print-expr-type', '-x', expr], stderr=subprocess.DEVNULL, stdout=subprocess.PIPE)
    return result.stdout.decode('utf-8').splitlines()[0].strip()

def determine_number_of_params(prim_name: str) -> int:
    for i in range(100):
        result = subprocess.run(['./bin/wake', '-x', prim_to_expr(prim_name, i)], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        if result.returncode == 0:
            return i

    print("Unable to determine number of params under 100 tries, does prim have >100 parameters?")
    exit(1)

def prim_to_def(name: str, expr: str, type: str) -> str:
    def_str = "export def prim_" + name + ": " + type + " =" + "\n"
    def_str += "    " + expr
    return def_str


def main():
    result = subprocess.run(['./bin/wake', '--list-prims'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

    print("package prim_wake")
    print("")

    for prim in result.stdout.decode('utf-8').splitlines():
        prim = prim.strip()
        if prim == "":
            continue
        num_params = determine_number_of_params(prim)
        expr = prim_to_expr(prim, num_params)
        type = determine_expr_type(expr)
        print(prim_to_def(prim, expr, type))
        print("")

if __name__ == "__main__":
    main()
