# Copyright 2022 SiFive, Inc.
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

from os import listdir
from os.path import isfile, join
import json

out = []
cmd_dir = "compile_commands"
for compile_command_path in [f for f in listdir(cmd_dir) if isfile(join(cmd_dir, f))]:
    with open(join(cmd_dir, compile_command_path)) as compile_command_file:
        cmd_json = json.load(compile_command_file)
        out.append(cmd_json)

with open("compile_commands.json", "w") as compile_cmd_json:
    json.dump(out, compile_cmd_json, indent=2)
