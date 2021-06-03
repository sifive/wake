import os
import subprocess

'''
    Returns a dictionary that maps file locations to functions.
'''
def file_loc_to_sig_dict():
    environment = os.environ
    subprocess.run(["wake", "--init", "."])
    file_contents = subprocess.run(["wake", "-e", "--in", "wake"], stdout=subprocess.PIPE, env=environment).stdout
    features_list = file_contents.decode("utf-8").split('\n')
    loc_to_feature_dict = {}
    for feature in features_list:
        if '= <' in feature:
            index_of_equals = feature.index('= <')
            sig = feature[0:index_of_equals].strip()
            loc = feature[index_of_equals+3:len(feature)-1].strip()
            last_colon = loc.rindex(":")
            loc = loc[0:last_colon]
            if '[' in loc:
                left_bracket = loc.rindex('[')
                dash = loc.index('-')
                loc = loc[0:left_bracket] + loc[left_bracket+1:dash]
            loc_to_feature_dict[loc] = sig
    return loc_to_feature_dict

'''
    Returns the text of the RST for a certain specified file
'''
def wake_rst_text_improved(filename):
    length = len(filename)
    locations = file_loc_to_sig_dict()
    assert(filename[length-5:length]) == ".wake" # check that this is a .wake file
    f = open(filename)
    contents = f.read()
    line_list = list(contents.split('\n'))
    sig_comment_types = []
    for i in range(len(line_list)):
        line = line_list[i]
        if line[0:6] == "export":
            equals_sign = None
            if ' = ' in line:
                equals_sign = line.index(' = ')
            elif ' =' in line:
                equals_sign = line.index(' =')
            sig = line[7:equals_sign].strip()
            dec_type = None
            if sig[0:3] == 'def':
                dec_type = 'function'
            elif sig[0:5] == 'tuple':
                dec_type = 'tuple'
            else:
                dec_type = 'data'
            j = i - 1
            comment = ""
            while j > 0 and line_list[j] != "" and line_list[j][0] == '#':
                comment = line_list[j][1:].strip() + '\n\t' + comment
                j -= 1
            if comment == "":
                comment = "No description for this feature yet."
            path_line_num = filename + ":" + str(i + 1)
            if path_line_num in locations.keys():
                func_type = locations[path_line_num]
                first_colon = func_type.index(":")
                func_type = func_type[first_colon+1:].strip()
                comment = comment.replace('[[', ':wake:reref:`')
                comment = comment.replace(']]', '`')
                sig_comment_types += [(sig, comment, dec_type, func_type)]
    document = ""
    for sig, comm, dec_type, types in sig_comment_types:
        last_arrow = -2
        params = ""
        if "=>" in types:
            last_arrow = types.rindex("=>")
            params = types[0:last_arrow].strip()
            params = params.replace(" =>", ", ")
            params = params.replace("(", "")
            params = params.replace(")", "")
        ret_type = types[last_arrow+2:].strip()
        document += ".. wake:" + dec_type + ":: " + sig + "\n\n\t" + comm + "\n\n"
        if params != "":
            params = '``' + params + '``'
        else:
            params = "None"
        if ret_type != "":
            ret_type = '``' + ret_type + '``'
        else:
            params = "None"
        if (dec_type != 'data'):
            document += "\tParameters: " + params + "\n\n\tReturn Type: " + ret_type + "\n\n"
    return document

'''
    Generates all the .rst files for the specified directory.
'''
def generate_all_rst(root_directory, output):
    dirs = list(os.walk(root_directory))
    print(dirs)
    for directory, folders, files in dirs:
        reldir = directory[len(root_directory)+1:]
        toctree = '-' * len(reldir) + '\n'
        toctree += reldir + '\n'
        toctree += '-' * len(reldir) + '\n\n'
        toctree += ".. toctree::\n"
        for folder in folders:
            toctree += '\t' + folder + '/' + folder + '.rst\n'
        rsts = ''
        for file in files:
            if file[len(file)-5:len(file)] == '.wake':
                rsts += file + '\n' + '-' * len(file) + '\n'
                rsts += wake_rst_text_improved(directory + '/' + file)
        dir_folder = directory
        if ('/' in directory):
            dir_folder = directory[directory.rindex('/')+1:]
        if not os.path.exists(output + '/source/' + reldir):
            os.mkdir(output + '/source/' + reldir)
        rst_output = open(output + '/source/' + reldir + '/' + dir_folder + ".rst", 'w')
        rst_output.write(toctree + '\n\n' + rsts)
        rst_output.close()
        
generate_all_rst('share/wake/lib', 'scripts/sphinx')
os.chdir('scripts/sphinx')
subprocess.run(['make', 'html'])
